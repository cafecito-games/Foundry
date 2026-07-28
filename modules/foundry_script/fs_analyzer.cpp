/**************************************************************************/
/*  fs_analyzer.cpp                                                       */
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

#include "fs_analyzer.h"

#include "foundry_script.h"
#include "fs_script_extensible_native_hooks.h"
#include "fs_tagged_union.h"
#include "fs_trait_utils.h"
#include "fs_type.h"
#include "fs_utility_callable.h"
#include "fs_utility_functions.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/core_constants.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/hashfuncs.h"
#include "scene/main/node.h"

#define UNNAMED_ENUM "<anonymous enum>"
#define ENUM_SEPARATOR "."

static thread_local String bootstrap_allowed_dependency_root;

const char *FSAnalyzer::analyzer_phase_name(AnalyzerPhase p_phase) {
	switch (p_phase) {
		case AnalyzerPhase::NONE:
			return "none";
		case AnalyzerPhase::PREFLIGHT:
			return "preflight";
		case AnalyzerPhase::DEPENDENCY_PARSE_AVAILABILITY:
			return "dependency_parse_availability";
		case AnalyzerPhase::INHERITANCE_RESOLUTION:
			return "inheritance_resolution";
		case AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE:
			return "interface_and_member_surface";
		case AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION:
			return "trait_conformance_registration";
		case AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL:
			return "body_expression_callable_signal";
		case AnalyzerPhase::FLOW_FINALITY_INVARIANTS:
			return "flow_finality_invariants";
		case AnalyzerPhase::CONFORMANCE_WITNESS_BODY:
			return "conformance_witness_body";
		case AnalyzerPhase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES:
			return "final_diagnostics_and_dependencies";
	}
	return "unknown";
}

FSAnalyzer::AnalyzerPhase FSAnalyzer::analyzer_phase_predecessor(AnalyzerPhase p_phase) {
	switch (p_phase) {
		case AnalyzerPhase::NONE:
		case AnalyzerPhase::PREFLIGHT:
		case AnalyzerPhase::DEPENDENCY_PARSE_AVAILABILITY:
		case AnalyzerPhase::INHERITANCE_RESOLUTION:
			return AnalyzerPhase::NONE;
		case AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE:
			return AnalyzerPhase::INHERITANCE_RESOLUTION;
		case AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION:
			return AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE;
		case AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL:
			return AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE;
		case AnalyzerPhase::FLOW_FINALITY_INVARIANTS:
			return AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL;
		case AnalyzerPhase::CONFORMANCE_WITNESS_BODY:
			return AnalyzerPhase::FLOW_FINALITY_INVARIANTS;
		case AnalyzerPhase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES:
			return AnalyzerPhase::NONE;
	}
	return AnalyzerPhase::NONE;
}

String FSAnalyzer::make_analyzer_phase_order_violation_message(AnalyzerPhase p_requested_phase, AnalyzerPhase p_required_predecessor) const {
	const String script_path = parser != nullptr ? parser->script_path : String("<unknown>");
	return vformat(
			"FSAnalyzer phase order violation: requested phase '%s' requires predecessor phase '%s', but current completed phase is '%s' (parser: '%s').",
			analyzer_phase_name(p_requested_phase),
			analyzer_phase_name(p_required_predecessor),
			analyzer_phase_name(highest_completed_phase),
			script_path);
}

void FSAnalyzer::require_completed_analyzer_phase(AnalyzerPhase p_required_predecessor, AnalyzerPhase p_requested_phase) const {
	if (suppress_internal_phase_order_checks || p_required_predecessor == AnalyzerPhase::NONE) {
		return;
	}
	if (highest_completed_phase >= p_required_predecessor) {
		return;
	}
	if (parser != nullptr && parser->head != nullptr) {
		if (p_required_predecessor == AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE && parser->head->resolved_interface) {
			return;
		}
		if (p_required_predecessor == AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL && parser->head->resolved_body) {
			return;
		}
		if (p_required_predecessor == AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION && parser->head->conformances.is_empty()) {
			return;
		}
		if (p_required_predecessor == AnalyzerPhase::FLOW_FINALITY_INVARIANTS &&
				highest_completed_phase >= AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL) {
			return;
		}
	}
#ifdef DEBUG_ENABLED
	ERR_FAIL_MSG(make_analyzer_phase_order_violation_message(p_requested_phase, p_required_predecessor));
#else
	return;
#endif
}

void FSAnalyzer::mark_analyzer_phase_completed(AnalyzerPhase p_phase) {
	if (p_phase > highest_completed_phase) {
		highest_completed_phase = p_phase;
	}
}

FSAnalyzer::AnalysisScopeGuard::AnalysisScopeGuard(FSAnalyzer *p_analyzer, FSParser::ClassNode *p_class, FSParser::FunctionNode *p_function) {
	analyzer = p_analyzer;
	if (analyzer == nullptr || analyzer->parser == nullptr) {
		return;
	}
	previous_class = analyzer->parser->current_class;
	previous_function = analyzer->parser->current_function;
	previous_enum = analyzer->current_enum;
	previous_lambda = analyzer->current_lambda;
	previous_static_context = analyzer->static_context;
	analyzer->parser->current_class = p_class;
	analyzer->parser->current_function = p_function;
}

FSAnalyzer::AnalysisScopeGuard::~AnalysisScopeGuard() {
	if (analyzer == nullptr || analyzer->parser == nullptr) {
		return;
	}
	analyzer->parser->current_class = previous_class;
	analyzer->parser->current_function = previous_function;
	analyzer->current_enum = previous_enum;
	analyzer->current_lambda = previous_lambda;
	analyzer->static_context = previous_static_context;
}

FSParser::DataType FSAnalyzer::enum_self_type(const FSParser::FunctionNode *p_function) const {
	const FSParser::FunctionNode *function = p_function;
	while (function != nullptr) {
		if (function->owner_enum != nullptr) {
			return type_from_metatype(function->owner_enum->get_datatype());
		}
		if (function->source_lambda == nullptr) {
			break;
		}
		function = function->source_lambda->parent_function;
	}
	return FSParser::DataType();
}

FSParser::EnumNode *FSAnalyzer::resolve_enum_declaration(const FSParser::DataType &p_enum_type,
		const FSParser::Node *p_source) {
	if (p_enum_type.kind != FSParser::DataType::ENUM || p_enum_type.class_type == nullptr) {
		return nullptr;
	}

	FSParser::ClassNode *owner = p_enum_type.class_type;
	resolve_class_interface(owner, p_source);

	if (owner->is_enum_file && owner->enum_file_decl != nullptr) {
		return owner->enum_file_decl;
	}
	if (!owner->has_member(p_enum_type.enum_type)) {
		return nullptr;
	}

	const FSParser::ClassNode::Member &member = owner->get_member(p_enum_type.enum_type);
	return member.type == FSParser::ClassNode::Member::ENUM ? member.m_enum : nullptr;
}

FSAnalyzer::DependencyParserAccess::DependencyParserAccess(FSAnalyzer *p_analyzer) {
	analyzer = p_analyzer;
}

void FSAnalyzer::DependencyParserAccess::mark_dependency_phase_completed() {
	if (analyzer != nullptr) {
		analyzer->mark_analyzer_phase_completed(AnalyzerPhase::DEPENDENCY_PARSE_AVAILABILITY);
	}
}

Error FSAnalyzer::DependencyParserAccess::raise_parser_to_status(const Ref<FSParserRef> &p_parser_ref, FSParserRef::Status p_required_status) {
	if (p_parser_ref.is_null()) {
		return ERR_PARSE_ERROR;
	}
	return p_parser_ref->raise_status(p_required_status);
}

Ref<FSParserRef> FSAnalyzer::DependencyParserAccess::depended_parser_for(const String &p_path, FSParserRef::Status p_required_status) {
	Ref<FSParserRef> parser_ref;
	raise_depended_parser_for(p_path, p_required_status, parser_ref);
	return parser_ref;
}

Error FSAnalyzer::DependencyParserAccess::raise_depended_parser_for(const String &p_path, FSParserRef::Status p_required_status, Ref<FSParserRef> &r_parser_ref) {
	mark_dependency_phase_completed();
	r_parser_ref = Ref<FSParserRef>();
	if (analyzer == nullptr || analyzer->parser == nullptr) {
		return ERR_PARSE_ERROR;
	}
	r_parser_ref = analyzer->parser->get_depended_parser_for(p_path);
	return raise_parser_to_status(r_parser_ref, p_required_status);
}

FSAnalyzer::DependencyParserAccessScope::DependencyParserAccessScope(FSAnalyzer *p_analyzer) {
	analyzer = p_analyzer;
	if (analyzer != nullptr) {
		analyzer->mark_analyzer_phase_completed(AnalyzerPhase::DEPENDENCY_PARSE_AVAILABILITY);
	}
}

FSAnalyzer::DependencyParserAccess &FSAnalyzer::DependencyParserAccessScope::access() {
	ERR_FAIL_NULL_V(analyzer, analyzer->dependency_parser_access);
	return analyzer->dependency_parser_access;
}

FSAnalyzer::DependencyParserAccessScope::~DependencyParserAccessScope() {
}

FSAnalyzer::PendingLambdaBodiesScope::PendingLambdaBodiesScope(FSAnalyzer *p_analyzer) {
	analyzer = p_analyzer;
}

FSAnalyzer::PendingLambdaBodiesScope::~PendingLambdaBodiesScope() {
	if (analyzer == nullptr) {
		return;
	}
	if (!analyzer->pending_body_resolution_lambdas.is_empty()) {
		ERR_PRINT("FoundryScript bug (please report): Not all pending lambda bodies were resolved in time.");
		analyzer->resolve_pending_lambda_bodies();
	}
}

FSAnalyzer::AnalyzerPhaseScope::AnalyzerPhaseScope(FSAnalyzer *p_analyzer, AnalyzerPhase p_phase) :
		analyzer(p_analyzer), phase(p_phase) {
	if (analyzer == nullptr) {
		return;
	}
	const AnalyzerPhase required = analyzer_phase_predecessor(phase);
	analyzer->require_completed_analyzer_phase(required, phase);
}

void FSAnalyzer::AnalyzerPhaseScope::complete() {
	if (analyzer != nullptr && !completed) {
		analyzer->mark_analyzer_phase_completed(phase);
		completed = true;
	}
}

FSAnalyzer::AnalyzerPhaseScope::~AnalyzerPhaseScope() {
	complete();
}

// Phase 0 — Preflight
// Requires: parser source is parsed (`FSParserRef::PARSED` equivalent).
// Produces: refreshed autoload index, validated imports and annotation declarations, cleared stale errors.
// May report: import errors, annotation declaration errors, mixed-namespace debug warnings.
// Must not: mark class inheritance, interface, or body as solved.
Error FSAnalyzer::run_phase_preflight() {
	AnalyzerPhaseScope phase_scope(this, AnalyzerPhase::PREFLIGHT);
	parser->errors.clear();
	ensure_autoload_index_current();

	Error err = validate_imports();
	if (err) {
		return err;
	}

	err = validate_annotation_declarations();
	if (err) {
		return err;
	}

#ifdef DEBUG_ENABLED
	validate_mixed_namespace_directory();
#endif // DEBUG_ENABLED

	return OK;
}

// Phase 2 — Inheritance resolution
// Requires: preflight completed for full `analyze()` runs; parse availability for incremental callers.
// Produces: solved `extends` bases, native/script base metadata, nested class inheritance.
// May report: inheritance, cyclic reference, global class collision, and super-class resolution errors.
// Must not: resolve ordinary function bodies.
Error FSAnalyzer::run_phase_inheritance_resolution() {
	AnalyzerPhaseScope phase_scope(this, AnalyzerPhase::INHERITANCE_RESOLUTION);
	ensure_autoload_index_current();
	Error err = resolve_class_inheritance(parser->head, true);
	return err;
}

// Phase 3 — Interface and member surface resolution
// Requires: inheritance resolution for owned classes.
// Produces: trait uses, member signatures, enum values, property/signal/function surfaces.
// May report: member conflicts, trait use errors, signature and annotation surface diagnostics.
// Must not: analyze ordinary method bodies.
Error FSAnalyzer::run_phase_interface_and_member_surface() {
	AnalyzerPhaseScope phase_scope(this, AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE);
	ensure_autoload_index_current();
	Error err = resolve_trait_uses(parser->head, true);
	if (err) {
		return err;
	}

	resolve_class_interface(parser->head, true);
	return OK;
}

// Phase 4 — Trait conformance registration
// Requires: class interfaces available for conformance targets and traits.
// Produces: registered retroactive conformances and validated witness signatures.
// May report: conformance coherence, requirement satisfaction, and witness collision diagnostics.
// Must not: resolve witness bodies.
Error FSAnalyzer::run_phase_trait_conformance_registration() {
	AnalyzerPhaseScope phase_scope(this, AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION);
	resolve_conformances(parser->head);

	// Validate custom annotation declaration signatures after the class interface so constant
	// defaults can reference resolved members and constants. Running it here resolves declarations
	// both for the head (via `analyze()`) and for imported files raised to `INTERFACE_SOLVED`,
	// which lets import-aware usage resolution read another file's parameter types and defaults.
	resolve_annotation_declaration_signatures();

	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

// Phase 5 — Body, expression, callable, and signal analysis
// Requires: interface surfaces solved for the analyzed class.
// Produces: resolved method bodies, inline accessors, lambdas, expression types, callable/signal checks.
// May report: body typing, callable/signal, strict null/dynamic, and flow-narrowing diagnostics.
// Must not: run final member/static/local assignment invariants or conformance witness bodies.
Error FSAnalyzer::run_phase_body_expression_callable_signal() {
	ensure_autoload_index_current();
	resolve_class_body(parser->head, true);
	mark_analyzer_phase_completed(AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL);
	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

// Phase 6 — Flow/finality and trait body invariants
// Requires: relevant bodies for `p_class` are resolved.
// Produces: validated abstract requirements, trait conflicts/requirements, final assignment checks.
// May report: abstract implementation, trait conflict/requirement, final assignment, unreachable warnings.
// Must not: resolve conformance witness bodies.
void FSAnalyzer::run_phase_flow_finality_invariants(FSParser::ClassNode *p_class) {
	AnalyzerPhaseScope phase_scope(this, AnalyzerPhase::FLOW_FINALITY_INVARIANTS);
	validate_trait_conflicts(p_class);
	validate_trait_requirements(p_class);
	flow_finality.check_final_member_assignments(p_class);
	flow_finality.check_final_static_assignments(p_class);
	flow_finality.check_final_local_assignments(p_class);
}

// Phase 7 — Conformance witness body analysis
// Requires: conformance registration completed.
// Produces: resolved witness bodies with `parser->current_class` bound to the conformance target.
// May report: witness body typing and member-access diagnostics.
// Must not: register new conformances.
Error FSAnalyzer::run_phase_conformance_witness_body() {
	AnalyzerPhaseScope phase_scope(this, AnalyzerPhase::CONFORMANCE_WITNESS_BODY);
	resolve_conformance_bodies(parser->head);
	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

#ifdef TESTS_ENABLED
bool FSAnalyzer::test_would_violate_phase_order(AnalyzerPhase p_requested_phase, AnalyzerPhase p_required_predecessor) const {
	if (p_required_predecessor == AnalyzerPhase::NONE) {
		return false;
	}
	if (highest_completed_phase >= p_required_predecessor) {
		return false;
	}
	if (parser != nullptr && parser->head != nullptr) {
		if (p_required_predecessor == AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE && parser->head->resolved_interface) {
			return false;
		}
		if (p_required_predecessor == AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL && parser->head->resolved_body) {
			return false;
		}
		if (p_required_predecessor == AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION && parser->head->conformances.is_empty()) {
			return false;
		}
	}
	return true;
}

String FSAnalyzer::test_format_phase_order_violation(AnalyzerPhase p_requested_phase, AnalyzerPhase p_required_predecessor) const {
	return make_analyzer_phase_order_violation_message(p_requested_phase, p_required_predecessor);
}

FSParserRef::Status FSAnalyzer::test_get_depended_parser_status(const FSAnalyzer *p_analyzer, const String &p_path) {
	if (p_analyzer == nullptr || p_analyzer->parser == nullptr) {
		return FSParserRef::EMPTY;
	}
	const HashMap<String, Ref<FSParserRef>> &depended = p_analyzer->parser->get_depended_parsers();
	if (const Ref<FSParserRef> *found = depended.getptr(p_path)) {
		if (found->is_valid()) {
			return (*found)->get_status();
		}
	}
	return FSParserRef::EMPTY;
}

Ref<FSParserRef> FSAnalyzer::test_get_depended_parser_ref(const FSAnalyzer *p_analyzer, const String &p_path) {
	if (p_analyzer == nullptr || p_analyzer->parser == nullptr) {
		return Ref<FSParserRef>();
	}
	const HashMap<String, Ref<FSParserRef>> &depended = p_analyzer->parser->get_depended_parsers();
	if (const Ref<FSParserRef> *found = depended.getptr(p_path)) {
		return *found;
	}
	return Ref<FSParserRef>();
}

int FSAnalyzer::test_get_external_parser_cache_size(const FSAnalyzer *p_analyzer) {
	if (p_analyzer == nullptr) {
		return 0;
	}
	return p_analyzer->dependency_parser_access.test_external_class_parser_cache_size();
}
#endif // TESTS_ENABLED

static String _normalize_bootstrap_path(const String &p_path) {
	return ResourceUID::ensure_path(p_path).replace_char('\\', '/').simplify_path();
}

static bool _bootstrap_path_is_within_root(const String &p_path, const String &p_root) {
	if (p_path.is_empty() || p_root.is_empty()) {
		return false;
	}

	const String path = _normalize_bootstrap_path(p_path);
	String root = _normalize_bootstrap_path(p_root);
	if (!root.ends_with("/")) {
		root += "/";
	}

	return path == root.trim_suffix("/") || path.begins_with(root);
}

static FSParser::DataType _class_type_parameter_handle(
		const FSParser::TypeParameterNode *p_parameter,
		int p_index) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::TYPE_PARAMETER;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.type_parameter_scope = FSParser::DataType::TYPE_PARAMETER_CLASS;
	type.type_parameter_index = p_index;
	if (p_parameter != nullptr && p_parameter->identifier != nullptr) {
		type.type_parameter_name = p_parameter->identifier->name;
	}
	if (p_parameter != nullptr && p_parameter->resolved_bound.is_set() && !p_parameter->resolved_bound.is_variant()) {
		type.type_parameter_bound.push_back(p_parameter->resolved_bound);
	}
	return type;
}

static FSParser::DataType _self_type_for_class(FSParser::ClassNode *p_class) {
	FSParser::DataType self_type;
	if (p_class != nullptr) {
		self_type = p_class->get_datatype();
		self_type.is_meta_type = false;
		self_type.type_arguments.clear();
		for (int i = 0; i < p_class->type_parameters.size(); i++) {
			self_type.type_arguments.push_back(_class_type_parameter_handle(p_class->type_parameters[i], i));
		}
	}
	return self_type;
}

static bool _is_self_type_parameter(const FSParser::DataType &p_type) {
	return p_type.kind == FSParser::DataType::TYPE_PARAMETER && p_type.type_parameter_name == SNAME("@Self");
}

static bool _is_bare_self_value_parameter(const FSParser::DataType &p_type) {
	return _is_self_type_parameter(p_type) && !p_type.is_type_handle_annotation;
}

static FSParser::DataType _self_type_parameter_from_bound(const FSParser::DataType &p_bound) {
	FSParser::DataType self_type;
	self_type.kind = FSParser::DataType::TYPE_PARAMETER;
	self_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	self_type.type_parameter_name = SNAME("@Self");
	self_type.type_parameter_scope = FSParser::DataType::TYPE_PARAMETER_CLASS;
	self_type.type_parameter_index = -1;
	if (p_bound.is_set() && !p_bound.is_variant()) {
		self_type.type_parameter_bound.push_back(p_bound);
	}
	return self_type;
}

static FSParser::DataType _self_type_parameter_for_class(FSParser::ClassNode *p_class) {
	return _self_type_parameter_from_bound(_self_type_for_class(p_class));
}

static bool _datatype_contains_self_type_parameter(const FSParser::DataType &p_type) {
	if (_is_self_type_parameter(p_type)) {
		return true;
	}
	for (const FSParser::DataType &element : p_type.container_element_types) {
		if (_datatype_contains_self_type_parameter(element)) {
			return true;
		}
	}
	for (const FSParser::DataType &argument : p_type.type_arguments) {
		if (_datatype_contains_self_type_parameter(argument)) {
			return true;
		}
	}
	for (const FSParser::DataType &parameter_type : p_type.method_parameter_types) {
		if (_datatype_contains_self_type_parameter(parameter_type)) {
			return true;
		}
	}
	for (const FSParser::DataType &return_type : p_type.method_return_type) {
		if (_datatype_contains_self_type_parameter(return_type)) {
			return true;
		}
	}
	return false;
}

static bool _datatype_container_element_contains_self_type_parameter(const FSParser::DataType &p_type) {
	for (const FSParser::DataType &element : p_type.container_element_types) {
		if (_datatype_contains_self_type_parameter(element)) {
			return true;
		}
	}
	return false;
}

static bool _datatype_represents_final_class(const FSParser::DataType &p_type) {
	return p_type.kind == FSParser::DataType::CLASS && p_type.class_type != nullptr && p_type.class_type->is_final;
}

static bool _datatype_alpha_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b);
static bool _datatype_strict_identity_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b);
static FSParser::DataType type_handle_represented_type(const FSParser::DataType &p_type);
static bool _type_handle_source_is_handle(const FSParser::DataType &p_type);

static FSParser::DataType _substitute_self_type_parameter_with_bounds(const FSParser::DataType &p_type) {
	if (_is_self_type_parameter(p_type)) {
		if (p_type.type_parameter_bound.is_empty()) {
			return p_type;
		}
		FSParser::DataType result = p_type.type_parameter_bound[0];
		if (p_type.is_type_handle_annotation) {
			result.is_meta_type = true;
			result.is_type_handle_annotation = true;
			result.is_pseudo_type = false;
			result.is_constant = p_type.is_constant;
		}
		result.is_nullable = result.is_nullable || p_type.is_nullable;
		return result;
	}

	FSParser::DataType result = p_type;
	for (int i = 0; i < result.container_element_types.size(); i++) {
		result.container_element_types.write[i] =
				_substitute_self_type_parameter_with_bounds(result.container_element_types[i]);
	}
	for (int i = 0; i < result.type_arguments.size(); i++) {
		result.type_arguments.write[i] = _substitute_self_type_parameter_with_bounds(result.type_arguments[i]);
	}
	for (int i = 0; i < result.method_parameter_types.size(); i++) {
		result.method_parameter_types.write[i] =
				_substitute_self_type_parameter_with_bounds(result.method_parameter_types[i]);
	}
	for (int i = 0; i < result.method_return_type.size(); i++) {
		result.method_return_type.write[i] = _substitute_self_type_parameter_with_bounds(result.method_return_type[i]);
	}
	return result;
}

static bool _datatype_self_bindings_are_final(const FSParser::DataType &p_type) {
	if (_is_self_type_parameter(p_type)) {
		return !p_type.type_parameter_bound.is_empty() && _datatype_represents_final_class(p_type.type_parameter_bound[0]);
	}

	for (const FSParser::DataType &element : p_type.container_element_types) {
		if (!_datatype_self_bindings_are_final(element)) {
			return false;
		}
	}
	for (const FSParser::DataType &argument : p_type.type_arguments) {
		if (!_datatype_self_bindings_are_final(argument)) {
			return false;
		}
	}
	for (const FSParser::DataType &parameter_type : p_type.method_parameter_types) {
		if (!_datatype_self_bindings_are_final(parameter_type)) {
			return false;
		}
	}
	for (const FSParser::DataType &return_type : p_type.method_return_type) {
		if (!_datatype_self_bindings_are_final(return_type)) {
			return false;
		}
	}
	return true;
}

static bool _datatype_matches_self_return_contract(
		const FSParser::DataType &p_expected_type,
		const FSParser::DataType &p_result_type) {
	if (p_expected_type.is_nullable &&
			p_result_type.kind == FSParser::DataType::BUILTIN &&
			p_result_type.builtin_type == Variant::NIL) {
		return true;
	}
	const FSParser::DataType expected_type = _substitute_self_type_parameter_with_bounds(p_expected_type);
	if (expected_type.is_type_handle_annotation) {
		if (!_type_handle_source_is_handle(p_result_type)) {
			return false;
		}
		const FSParser::DataType result_handle_type = type_handle_represented_type(p_result_type);
		if (_datatype_strict_identity_equal(type_handle_represented_type(p_expected_type), result_handle_type)) {
			return true;
		}
		const FSParser::DataType expected_handle_type = type_handle_represented_type(expected_type);
		if (_datatype_contains_self_type_parameter(p_expected_type) && !_datatype_self_bindings_are_final(p_expected_type)) {
			return false;
		}
		return _datatype_strict_identity_equal(expected_handle_type, result_handle_type);
	}
	if (_datatype_alpha_equal(p_result_type, p_expected_type)) {
		return true;
	}
	if (p_expected_type.is_nullable) {
		FSParser::DataType non_nullable_expected = p_expected_type;
		non_nullable_expected.is_nullable = false;
		if (_datatype_alpha_equal(p_result_type, non_nullable_expected)) {
			return true;
		}
	}
	if (_datatype_contains_self_type_parameter(p_expected_type) && _datatype_self_bindings_are_final(p_expected_type)) {
		if (_datatype_strict_identity_equal(p_result_type, expected_type)) {
			return true;
		}
		if (expected_type.is_nullable) {
			FSParser::DataType non_nullable_expected = expected_type;
			non_nullable_expected.is_nullable = false;
			return _datatype_strict_identity_equal(p_result_type, non_nullable_expected);
		}
	}
	return false;
}

static bool _datatype_matches_self_parameter_contract(
		const FSParser::DataType &p_expected_type,
		const FSParser::DataType &p_argument_type) {
	if (_datatype_strict_identity_equal(p_expected_type, p_argument_type)) {
		return true;
	}
	if (p_expected_type.is_nullable) {
		FSParser::DataType non_nullable_expected = p_expected_type;
		non_nullable_expected.is_nullable = false;
		if (_datatype_strict_identity_equal(non_nullable_expected, p_argument_type)) {
			return true;
		}
	}
	const FSParser::DataType expected_type = _substitute_self_type_parameter_with_bounds(p_expected_type);
	if (expected_type.is_nullable &&
			p_argument_type.kind == FSParser::DataType::BUILTIN &&
			p_argument_type.builtin_type == Variant::NIL) {
		return true;
	}
	if (expected_type.is_type_handle_annotation) {
		if (!_type_handle_source_is_handle(p_argument_type)) {
			return false;
		}
		const FSParser::DataType argument_handle_type = type_handle_represented_type(p_argument_type);
		if (_datatype_strict_identity_equal(type_handle_represented_type(p_expected_type), argument_handle_type)) {
			return true;
		}
		if (_datatype_contains_self_type_parameter(p_expected_type) && !_datatype_self_bindings_are_final(p_expected_type)) {
			return false;
		}
		return _datatype_strict_identity_equal(type_handle_represented_type(expected_type), argument_handle_type);
	}
	if (!_datatype_self_bindings_are_final(p_expected_type)) {
		return false;
	}
	if (_datatype_strict_identity_equal(expected_type, p_argument_type)) {
		return true;
	}
	if (expected_type.is_nullable) {
		FSParser::DataType non_nullable_expected = expected_type;
		non_nullable_expected.is_nullable = false;
		return _datatype_strict_identity_equal(non_nullable_expected, p_argument_type);
	}
	return false;
}

static String identifier_name_from_expression(const FSParser::ExpressionNode *p_expression) {
	if (p_expression != nullptr && p_expression->type == FSParser::Node::IDENTIFIER) {
		const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
		return identifier->name;
	}
	return String();
}

static bool expression_is_same_reference(const FSParser::ExpressionNode *p_left, const FSParser::ExpressionNode *p_right) {
	if (p_left == nullptr || p_right == nullptr || p_left->type != p_right->type) {
		return false;
	}
	if (p_left->type == FSParser::Node::SELF) {
		return true;
	}
	if (p_left->type == FSParser::Node::IDENTIFIER) {
		const FSParser::IdentifierNode *left = static_cast<const FSParser::IdentifierNode *>(p_left);
		const FSParser::IdentifierNode *right = static_cast<const FSParser::IdentifierNode *>(p_right);
		return left->name == right->name;
	}
	return false;
}

static bool call_argument_is_same_receiver(
		const FSParser::CallNode *p_call,
		const FSParser::ExpressionNode *p_argument) {
	if (p_call == nullptr || p_argument == nullptr) {
		return false;
	}
	if (p_call->is_super || p_call->get_callee_type() == FSParser::Node::IDENTIFIER) {
		return p_argument->type == FSParser::Node::SELF;
	}
	if (p_call->get_callee_type() != FSParser::Node::SUBSCRIPT) {
		return false;
	}
	const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
	if (!subscript->is_attribute || subscript->base == nullptr) {
		return false;
	}
	return expression_is_same_reference(subscript->base, p_argument);
}

static String _class_or_trait_name(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return "<unknown>";
	}
	if (p_class->identifier != nullptr) {
		return p_class->identifier->name;
	}
	return p_class->fqcn.get_file();
}

static String _localize_script_path(const String &p_path) {
	if (ProjectSettings::get_singleton() == nullptr || p_path.is_empty()) {
		return p_path;
	}
	return ProjectSettings::get_singleton()->localize_path(p_path);
}

static String _trait_method_info_source(const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function) {
	if (p_class == nullptr) {
		return String();
	}

	String script_path = p_class->get_datatype().script_path;
	if (script_path.is_empty()) {
		script_path = p_class->fqcn;
	}
	if (script_path.is_empty()) {
		return String();
	}
	script_path = _localize_script_path(script_path);

	if (p_function != nullptr) {
		return vformat(R"(Implementation comes from "%s" line %d.)", script_path, p_function->start_line);
	}
	return vformat(R"(Implementation comes from "%s".)", script_path);
}

static MethodInfo info_from_utility_func(const StringName &p_function) {
	ERR_FAIL_COND_V(!Variant::has_utility_function(p_function), MethodInfo());

	MethodInfo info(p_function);

	if (Variant::has_utility_function_return_value(p_function)) {
		info.return_val.type = Variant::get_utility_function_return_type(p_function);
		if (info.return_val.type == Variant::NIL) {
			info.return_val.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
		}
	}

	if (Variant::is_utility_function_vararg(p_function)) {
		info.flags |= METHOD_FLAG_VARARG;
	} else {
		for (int i = 0; i < Variant::get_utility_function_argument_count(p_function); i++) {
			PropertyInfo pi;
#ifdef DEBUG_ENABLED
			pi.name = Variant::get_utility_function_argument_name(p_function, i);
#else
			pi.name = "arg" + itos(i + 1);
#endif // DEBUG_ENABLED
			pi.type = Variant::get_utility_function_argument_type(p_function, i);
			info.arguments.push_back(pi);
		}
	}

	return info;
}

static FSParser::DataType make_callable_type(const MethodInfo &p_info) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::BUILTIN;
	type.builtin_type = Variant::CALLABLE;
	type.is_constant = true;
	type.method_info = p_info;
	type.has_method_signature = true;
	// A reference to an async/coroutine method forms an AsyncCallable rather than a plain Callable.
	type.signature_is_async = (p_info.flags & METHOD_FLAG_ASYNC) != 0;
	return type;
}

static FSParser::DataType make_callable_type(const MethodInfo &p_info, const FSParser::FunctionNode *p_function) {
	FSParser::DataType type = make_callable_type(p_info);
	for (FSParser::ParameterNode *parameter : p_function->parameters) {
		type.method_parameter_types.push_back(parameter->get_datatype());
	}
	type.method_return_type.push_back(p_function->get_datatype());
	// A lambda/function that awaits is a coroutine, so the callable it forms is async.
	type.signature_is_async = type.signature_is_async || p_function->is_coroutine;
	return type;
}

// Wraps a result type T into the honest awaitable type Coroutine[T]. The principal identity is the
// native FSFunctionState (the value an unawaited async call actually has at runtime), skinned
// as "Coroutine" in to_string(); is_coroutine stays the discriminator reused by await / missing-await,
// and the phantom result type lives in container_element_types[0].
static FSParser::DataType make_coroutine_type(const FSParser::DataType &p_result_type) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = SNAME("FSFunctionState");
	type.is_coroutine = true;
	FSParser::DataType result_type = p_result_type;
	result_type.is_constant = false;
	result_type.is_meta_type = false;
	type.set_container_element_type(0, result_type);
	return type;
}

// A coroutine call whose live `FSFunctionState` handle is captured into a statically
// `Coroutine[T]`-typed slot (variable/parameter/return value, or a `Coroutine[T]` container
// element) is meant to be held and awaited later, not a forgotten `await`. Mark such a call so the
// compiler emits `OPCODE_CALL_ASYNC` (store the handle, skip the debug missing-await guard), just as
// it does for the operand of an `await`. The target must be a *hard* `Coroutine[T]` slot: a weakly
// inferred `var x = _job()` (no annotation, no `:=`) keeps a dynamic type and a capture into a
// non-coroutine/Variant slot is left unmarked, so in both cases the runtime guard still flags a
// genuinely missing `await`.
static void mark_coroutine_handle_capture(FSParser::ExpressionNode *p_expression, const FSParser::DataType &p_target_type) {
	if (p_expression == nullptr || !p_target_type.is_coroutine || !p_target_type.is_hard_type()) {
		return;
	}
	// The handle is produced by the coroutine call(s) that supply the captured value. Recurse through
	// the wrappers that forward a value unchanged -- a cast (`_job() as Coroutine[T]`) and a ternary
	// (`a if c else b`) -- so the inner calls are marked even when the captured expression is not a
	// direct call. An `await` is intentionally not followed: it unwraps to `T`, never `Coroutine[T]`.
	switch (p_expression->type) {
		case FSParser::Node::CALL: {
			FSParser::CallNode *call = static_cast<FSParser::CallNode *>(p_expression);
			if (call->get_datatype().is_coroutine) {
				call->is_coroutine_handle_capture = true;
			}
		} break;
		case FSParser::Node::CAST: {
			mark_coroutine_handle_capture(static_cast<FSParser::CastNode *>(p_expression)->operand, p_target_type);
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			FSParser::TernaryOpNode *ternary = static_cast<FSParser::TernaryOpNode *>(p_expression);
			mark_coroutine_handle_capture(ternary->true_expr, p_target_type);
			mark_coroutine_handle_capture(ternary->false_expr, p_target_type);
		} break;
		default:
			break;
	}
}

static FSParser::DataType make_signal_type(const MethodInfo &p_info) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::BUILTIN;
	type.builtin_type = Variant::SIGNAL;
	type.is_constant = true;
	type.method_info = p_info;
	type.has_method_signature = true;
	return type;
}

static FSParser::DataType make_native_enum_type(const StringName &p_enum_name, const StringName &p_native_class, bool p_meta);
static FSParser::DataType make_builtin_enum_type(const StringName &p_enum_name, Variant::Type p_type, bool p_meta);
static FSParser::DataType make_global_enum_type(const StringName &p_enum_name, const StringName &p_base, bool p_meta);
static FSParser::DataType make_standalone_global_enum_type(const StringName &p_global_name, bool p_meta);

// Rebuilds the enum identity behind a hint leaf — "Base.Member" for native-class/built-in enums or a
// bare name for global enums — so a nested enum slot compares exactly across the script-API boundary
// (enum equality keys on native_type). Only enums whose identity the flat grammar can reproduce qualify:
// global (CoreConstants), native-class (ClassDB), and built-in (Variant). A script/class enum has no
// stable global name here (the accepted non-global script/class leaf limitation), so it is left for the
// caller to degrade. This MUST stay in sync with the encoder's `_enum_signature_leaf_round_trips`
// (fs_parser.cpp). Returns false without touching r_type when p_name is not a reproducible enum.
static bool _resolve_hint_enum_leaf(const String &p_name, FSParser::DataType &r_type) {
	if (CoreConstants::is_global_enum(p_name)) {
		r_type = make_global_enum_type(p_name, StringName(), false);
		r_type.is_constant = false;
		return true;
	}
	if (ScriptServer::is_global_class(p_name) && ScriptServer::is_global_class_enum(p_name)) {
		r_type = make_standalone_global_enum_type(p_name, false);
		r_type.is_constant = false;
		return true;
	}
	const int separator = p_name.rfind(".");
	if (separator <= 0 || separator >= p_name.length() - 1) {
		return false;
	}
	const String base = p_name.substr(0, separator);
	const StringName enum_name = p_name.substr(separator + 1);
	if (ClassDB::class_exists(base) && ClassDB::has_enum(base, enum_name)) {
		r_type = make_native_enum_type(enum_name, base, false);
		r_type.is_constant = false;
		return true;
	}
	const Variant::Type base_builtin = FSParser::get_builtin_type(base);
	if (base_builtin < Variant::VARIANT_MAX && Variant::has_enum(base_builtin, enum_name)) {
		r_type = make_builtin_enum_type(enum_name, base_builtin, false);
		r_type.is_constant = false;
		return true;
	}
	return false;
}

// Resolves a single hint type-name (as produced by the PROPERTY_HINT_CALLABLE_TYPE / ARRAY_TYPE grammar)
// into a leaf DataType. Returns false if the name cannot be resolved.
static bool _resolve_hint_leaf_type(const StringName &p_name, FSParser::DataType &r_type) {
	r_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	r_type.is_constant = false;

	const Variant::Type builtin_type = FSParser::get_builtin_type(p_name);
	if (builtin_type < Variant::VARIANT_MAX) {
		r_type.kind = FSParser::DataType::BUILTIN;
		r_type.builtin_type = builtin_type;
		return true;
	}
	if (FSAnalyzer::class_exists(p_name)) {
		r_type.kind = FSParser::DataType::NATIVE;
		r_type.builtin_type = Variant::OBJECT;
		r_type.native_type = p_name;
		return true;
	}
	if (ScriptServer::is_global_class(p_name) && ScriptServer::is_global_class_enum(p_name)) {
		r_type = make_standalone_global_enum_type(p_name, false);
		r_type.is_constant = false;
		return true;
	}
	if (ScriptServer::is_global_class(p_name)) {
		Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(p_name));
		if (script.is_valid()) {
			r_type.kind = FSParser::DataType::SCRIPT;
			r_type.builtin_type = Variant::OBJECT;
			r_type.native_type = script->get_instance_base_type();
			r_type.script_type = script;
			return true;
		}
	}
	if (_resolve_hint_enum_leaf(p_name, r_type)) {
		return true;
	}
	return false;
}

// Splits a comma-separated list at bracket depth zero, so nested "Array[int]" / "Callable[[...]]"
// are not split internally. Trims surrounding whitespace on each element.
static Vector<String> _split_signature_top_level(const String &p_text) {
	Vector<String> parts;
	int depth = 0;
	int start = 0;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t character = p_text[i];
		if (character == '[') {
			depth++;
		} else if (character == ']') {
			depth--;
		} else if (character == ',' && depth == 0) {
			parts.push_back(p_text.substr(start, i - start).strip_edges());
			start = i + 1;
		}
	}
	parts.push_back(p_text.substr(start).strip_edges());
	return parts;
}

static FSParser::DataType _decode_signature_type(const String &p_encoded);

// Rebuilds a Coroutine[T] from its encoded result-type element (see _encode_coroutine_result_element).
// An empty element yields a result-less coroutine (identity preserved, phantom result unknown because it
// was lossy or unspecified); "Variant" yields Coroutine[Variant]; "void" yields a NIL result; anything
// else is decoded through the shared signature grammar. Non-empty elements are wrapped via
// make_coroutine_type so the coroutine identity and phantom result type survive the boundary.
static FSParser::DataType _decode_coroutine_result_element(const String &p_element) {
	const String element = p_element.strip_edges();
	if (element.is_empty()) {
		FSParser::DataType coroutine;
		coroutine.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		coroutine.kind = FSParser::DataType::NATIVE;
		coroutine.builtin_type = Variant::OBJECT;
		coroutine.native_type = SNAME("FSFunctionState");
		coroutine.is_coroutine = true;
		return coroutine;
	}
	FSParser::DataType result_type;
	result_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	if (element == "Variant") {
		result_type.kind = FSParser::DataType::VARIANT;
	} else if (element == "void") {
		result_type.kind = FSParser::DataType::BUILTIN;
		result_type.builtin_type = Variant::NIL;
	} else {
		result_type = _decode_signature_type(element);
	}
	return make_coroutine_type(result_type);
}

// Decodes a Callable/Signal signature suffix ("[[p0, p1], ret]" or "[[p0, p1]]") into r_type. Returns
// false — leaving r_type untouched (a bare callable/signal) — when the suffix does not match the
// expected grammar, so malformed external metadata degrades to gradual typing instead of a bogus
// (e.g. zero-argument void) signature that would wrongly reject valid calls.
static bool _decode_method_signature_suffix(const String &p_suffix, bool p_has_return, FSParser::DataType &r_type) {
	// Validate the wrapper shape first: a well-formed suffix opens with the outer bracket plus the
	// params-block opener ("[["), closes on the outer bracket, and keeps brackets balanced throughout.
	if (!p_suffix.begins_with("[[") || !p_suffix.ends_with("]")) {
		return false;
	}
	int balance = 0;
	for (int i = 0; i < p_suffix.length(); i++) {
		if (p_suffix[i] == '[') {
			balance++;
		} else if (p_suffix[i] == ']') {
			balance--;
			if (balance < 0) {
				return false;
			}
		}
	}
	if (balance != 0) {
		return false;
	}

	const String inner = p_suffix.substr(1, p_suffix.length() - 2); // "[<params>], <ret>" or "[<params>]"

	// The parameter list is the first bracket-balanced "[...]" segment of `inner`.
	int depth = 0;
	int params_end = -1;
	for (int i = 0; i < inner.length(); i++) {
		if (inner[i] == '[') {
			depth++;
		} else if (inner[i] == ']') {
			depth--;
			if (depth == 0) {
				params_end = i;
				break;
			}
		}
	}
	if (params_end < 1) {
		return false;
	}

	Vector<FSParser::DataType> parameter_types;
	const String params_block = inner.substr(1, params_end - 1); // between the inner brackets
	const String trimmed_params = params_block.strip_edges();
	if (!trimmed_params.is_empty()) {
		for (const String &parameter : _split_signature_top_level(params_block)) {
			if (parameter.is_empty()) {
				continue;
			}
			parameter_types.push_back(_decode_signature_type(parameter));
		}
	}

	Vector<FSParser::DataType> return_types;
	const String rest = inner.substr(params_end + 1).strip_edges(); // text after the parameter block
	if (p_has_return) {
		// A Callable suffix is "[[...], ret]": the parameter block must be followed by ", <ret>".
		if (!rest.begins_with(",")) {
			return false;
		}
		const String return_name = rest.substr(1).strip_edges();
		if (return_name.is_empty()) {
			return false;
		}
		FSParser::DataType return_type;
		if (return_name == "void") {
			return_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
			return_type.kind = FSParser::DataType::BUILTIN;
			return_type.builtin_type = Variant::NIL;
		} else {
			return_type = _decode_signature_type(return_name);
		}
		return_types.push_back(return_type);
	} else if (!rest.is_empty()) {
		// A Signal suffix is "[[...]]": nothing may follow the parameter block.
		return false;
	}

	r_type.has_method_signature = true;
	r_type.has_explicit_method_signature = true;
	r_type.method_parameter_types = parameter_types;
	r_type.method_return_type = return_types;

	// Mirror the rich slots into method_info too. Callable compatibility falls back to a MethodInfo
	// comparison when one side is MethodInfo-only (e.g. a utility-function reference like `sin` built by
	// make_callable_type); without this mirror a decoded explicit callable would carry an empty
	// MethodInfo and wrongly reject an otherwise-matching MethodInfo-only callable.
	MethodInfo signature_info;
	for (const FSParser::DataType &parameter_type : parameter_types) {
		signature_info.arguments.push_back(parameter_type.to_property_info(""));
	}
	if (p_has_return && !return_types.is_empty()) {
		signature_info.return_val = return_types[0].to_property_info("");
	}
	r_type.method_info = signature_info;
	return true;
}

static FSParser::DataType _decode_signature_type_base(const String &p_encoded) {
	FSParser::DataType result;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	const String text = p_encoded.strip_edges();

	if (text == "Callable" || text.begins_with("Callable[")) {
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::CALLABLE;
		if (text.length() > 8) { // has a "[...]" suffix after "Callable"
			_decode_method_signature_suffix(text.substr(8), true, result);
		}
		return result;
	}
	// AsyncCallable is encoded under a distinct name (see _encode_signature_type_base) so the async
	// marker survives the boundary; rebuild it as a Callable that carries signature_is_async.
	if (text == "AsyncCallable" || text.begins_with("AsyncCallable[")) {
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::CALLABLE;
		result.signature_is_async = true;
		if (text.length() > 13) { // has a "[...]" suffix after "AsyncCallable"
			_decode_method_signature_suffix(text.substr(13), true, result);
		}
		return result;
	}
	if (text == "Signal" || text.begins_with("Signal[")) {
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::SIGNAL;
		if (text.length() > 6) {
			_decode_method_signature_suffix(text.substr(6), false, result);
		}
		return result;
	}
	if (text.begins_with("Array[") && text.ends_with("]")) {
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::ARRAY;
		const String element = text.substr(6, text.length() - 7); // between "Array[" and trailing "]"
		FSParser::DataType element_type = _decode_signature_type(element);
		element_type.is_constant = false;
		result.set_container_element_type(0, element_type);
		return result;
	}
	if (text.begins_with("Dictionary[") && text.ends_with("]")) {
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::DICTIONARY;
		const String pair = text.substr(11, text.length() - 12); // between "Dictionary[" and trailing "]"
		const Vector<String> key_value = _split_signature_top_level(pair);
		if (key_value.size() == 2) {
			FSParser::DataType key_type = _decode_signature_type(key_value[0]);
			FSParser::DataType value_type = _decode_signature_type(key_value[1]);
			key_type.is_constant = false;
			value_type.is_constant = false;
			result.set_container_element_type(0, key_type);
			result.set_container_element_type(1, value_type);
		}
		return result;
	}
	// Only the bracketed form is the coroutine skin: the reserved coroutine syntax always carries brackets
	// (see is_coroutine_type gating in the parser and _encode_signature_type_base, which always emits
	// "Coroutine[...]" for a genuine coroutine). A bare "Coroutine" leaf is an ordinary class/native literally
	// named "Coroutine" and must fall through to the leaf resolver, mirroring the container-element gating in
	// _container_element_hint_is_coroutine. Matching the bare token here would misdecode such a class as the
	// FSFunctionState coroutine skin across the Callable/Signal signature boundary.
	if (text.begins_with("Coroutine[") && text.ends_with("]")) {
		const String element = text.substr(10, text.length() - 11); // between "Coroutine[" and trailing "]"
		return _decode_coroutine_result_element(element);
	}
	if (_resolve_hint_leaf_type(text, result)) {
		return result;
	}
	// Unresolvable leaf: degrade to Variant rather than fail the whole decode.
	result.kind = FSParser::DataType::VARIANT;
	return result;
}

static FSParser::DataType _decode_signature_type(const String &p_encoded) {
	const String text = p_encoded.strip_edges();
	// A trailing `?` marks a nullable slot (encoded by _encode_signature_type). It only ever appears
	// as the final character of a whole type token; nested `T?` slots sit inside brackets and are
	// recovered by the recursive decode of each split element.
	if (text.ends_with("?")) {
		FSParser::DataType result = _decode_signature_type_base(text.substr(0, text.length() - 1));
		if (result.kind != FSParser::DataType::VARIANT) {
			result.is_nullable = true;
		}
		return result;
	}
	return _decode_signature_type_base(text);
}

// A typed-container element hint encodes a Coroutine[T] element through the signature grammar (see
// DataType::to_property_info's array/dictionary branches), distinct from the plain class-name leaf used
// for every other element kind. The decoder routes such an element through _decode_signature_type so the
// coroutine identity and phantom result T are rebuilt instead of resolving "Coroutine[...]" as a class.
// Only the bracketed form is a coroutine: a bare "Coroutine" is an ordinary class name (the reserved
// coroutine syntax always carries brackets), so the result-less element is encoded as "Coroutine[]".
static bool _container_element_hint_is_coroutine(const String &p_hint) {
	return p_hint.begins_with("Coroutine[") && p_hint.ends_with("]");
}

// A signature slot is comparison-safe across the script-API boundary when its kind survives a
// PropertyInfo round-trip unambiguously. A user script/class surfaces as SCRIPT when rebuilt from
// PropertyInfo but may be a CLASS handle in a local annotation, and the strict rich-slot comparator
// keys on kind; a script/class enum and type parameters are likewise ambiguous. A signature carrying
// such a slot is kept non-explicit so the MethodInfo fallback — which compares object slots by class
// name — decides compatibility instead (mirroring how the encoder side suppresses these hints). A
// global/native/built-in enum is the exception: its identity round-trips through the hint grammar, so
// it is safe to compare as a rich slot.
static bool _signature_slot_is_comparison_safe(const FSParser::DataType &p_type) {
	// Coroutine[T] is a NATIVE skin, but unlike a plain native it carries a phantom result type that only
	// survives the boundary when T itself round-trips. A result-less coroutine (its result was lossy and
	// dropped on encode, see DataType::to_property_info) must cross gradually, so require a comparison-safe
	// result element rather than falling through to the always-safe NATIVE case below.
	if (p_type.is_coroutine) {
		return p_type.has_container_element_type(0) && _signature_slot_is_comparison_safe(p_type.get_container_element_type(0));
	}
	switch (p_type.kind) {
		case FSParser::DataType::ENUM: {
			if (p_type.is_tagged_union) {
				// A tagged union has no flat-hint spelling, so it never round-trips as a comparable slot.
				return false;
			}
			FSParser::DataType reconstructed;
			return _resolve_hint_enum_leaf(String(p_type.native_type).replace("::", "."), reconstructed);
		}
		case FSParser::DataType::SCRIPT:
		case FSParser::DataType::CLASS:
		case FSParser::DataType::TUPLE:
		case FSParser::DataType::TYPE_PARAMETER:
		case FSParser::DataType::RESOLVING:
		case FSParser::DataType::UNRESOLVED:
			return false;
		case FSParser::DataType::BUILTIN:
			for (const FSParser::DataType &element_type : p_type.container_element_types) {
				if (!_signature_slot_is_comparison_safe(element_type)) {
					return false;
				}
			}
			for (const FSParser::DataType &parameter_type : p_type.method_parameter_types) {
				if (!_signature_slot_is_comparison_safe(parameter_type)) {
					return false;
				}
			}
			for (const FSParser::DataType &return_type : p_type.method_return_type) {
				if (!_signature_slot_is_comparison_safe(return_type)) {
					return false;
				}
			}
			return true;
		case FSParser::DataType::NATIVE:
		case FSParser::DataType::VARIANT:
			return true;
	}
	return true;
}

static bool _signature_is_comparison_safe(const Vector<FSParser::DataType> &p_parameter_types) {
	for (const FSParser::DataType &parameter_type : p_parameter_types) {
		if (!_signature_slot_is_comparison_safe(parameter_type)) {
			return false;
		}
	}
	return true;
}

static FSParser::DataType make_native_meta_type(const StringName &p_class_name) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_class_name;
	type.is_constant = true;
	type.is_meta_type = true;
	return type;
}

static FSParser::DataType make_script_meta_type(const Ref<Script> &p_script) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::SCRIPT;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_script->get_instance_base_type();
	type.script_type = p_script;
	type.script_path = p_script->get_path();
	type.is_constant = true;
	type.is_meta_type = true;
	return type;
}

static FSParser::DataType type_handle_represented_type(const FSParser::DataType &p_type) {
	FSParser::DataType represented_type = p_type;
	represented_type.is_type_handle_annotation = false;
	represented_type.is_meta_type = false;
	represented_type.is_pseudo_type = false;
	represented_type.is_constant = false;
	represented_type.is_nullable = false;
	return represented_type;
}

static String _type_handle_represented_type_name(
		const FSParser::DataType &p_type,
		const FSParser::Node *p_source_node = nullptr) {
	if (p_source_node != nullptr && p_source_node->is_expression()) {
		const FSParser::ExpressionNode *expression = static_cast<const FSParser::ExpressionNode *>(p_source_node);
		if (expression->is_constant && expression->reduced_value.get_type() == Variant::OBJECT) {
			bool was_freed = false;
			Object *object = expression->reduced_value.get_validated_object_with_check(was_freed);
			if (object != nullptr) {
				FSNativeClass *native_class = Object::cast_to<FSNativeClass>(object);
				if (native_class != nullptr) {
					return native_class->get_name();
				}
			}
		}
	}

	return type_handle_represented_type(p_type).to_string();
}

static bool _type_handle_source_is_handle(const FSParser::DataType &p_type) {
	return p_type.is_meta_type || p_type.is_type_handle_annotation;
}

static bool _type_handle_source_is_null(const FSParser::DataType &p_type) {
	return p_type.kind == FSParser::DataType::BUILTIN && p_type.builtin_type == Variant::NIL;
}

static String _make_type_handle_assignment_error(
		const FSParser::DataType &p_target_type,
		const FSParser::DataType &p_source_type,
		const FSParser::Node *p_source_node,
		const String &p_target_kind,
		const StringName &p_target_name,
		bool p_has_specified_type) {
	if (!p_target_type.is_type_handle_annotation || _type_handle_source_is_null(p_source_type)) {
		return String();
	}

	const String target_type = p_target_type.to_string();
	const String expected_represented_type = type_handle_represented_type(p_target_type).to_string();
	const String source_represented_type = _type_handle_represented_type_name(p_source_type, p_source_node);
	const bool source_is_handle = _type_handle_source_is_handle(p_source_type);

	String target_description;
	if (p_target_name != StringName()) {
		target_description = vformat(R"(%s "%s"%s "%s")",
				p_target_kind,
				p_target_name,
				p_has_specified_type ? " with specified type" : " of type",
				target_type);
	} else {
		target_description = vformat(R"(target of type "%s")", target_type);
	}

	if (source_is_handle) {
		return vformat(R"(Cannot assign class handle "%s" to %s; handle represents "%s", which is not compatible with "%s".)",
				source_represented_type,
				target_description,
				source_represented_type,
				expected_represented_type);
	}

	return vformat("Cannot assign instance value of type \"%s\" to %s; "
				   "expected a class handle whose represented instance type is \"%s\".",
			p_source_type.to_string(),
			target_description,
			expected_represented_type);
}

static String _make_type_handle_argument_error(
		const StringName &p_function,
		int p_argument_number,
		const FSParser::DataType &p_expected_type,
		const FSParser::DataType &p_actual_type,
		const FSParser::Node *p_actual_node) {
	if (!p_expected_type.is_type_handle_annotation || _type_handle_source_is_null(p_actual_type)) {
		return String();
	}

	const String expected_type = p_expected_type.to_string();
	const String expected_represented_type = type_handle_represented_type(p_expected_type).to_string();
	const String actual_represented_type = _type_handle_represented_type_name(p_actual_type, p_actual_node);
	if (_type_handle_source_is_handle(p_actual_type)) {
		return vformat("Cannot pass class handle \"%s\" as argument %d of \"%s()\"; "
					   "handle represents \"%s\", which is not compatible with expected represented instance type \"%s\" for \"%s\".",
				actual_represented_type,
				p_argument_number,
				p_function,
				actual_represented_type,
				expected_represented_type,
				expected_type);
	}

	return vformat("Cannot pass instance value of type \"%s\" as argument %d of \"%s()\"; "
				   "expected a class handle whose represented instance type is \"%s\" for \"%s\".",
			p_actual_type.to_string(),
			p_argument_number,
			p_function,
			expected_represented_type,
			expected_type);
}

FSParser::DataType FSAnalyzer::make_tuple_type(const StringName &p_tuple_name, const String &p_owner_fqcn, const String &p_script_path,
		const Vector<FSParser::DataType> &p_element_types, const Vector<StringName> &p_field_names, bool p_meta) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::TUPLE;
	// Every tuple erases to a read-only Array at runtime; the precise shape is static-only.
	type.builtin_type = Variant::ARRAY;
	type.tuple_name = p_tuple_name;
	// `native_type` carries the nominal identity, qualified by the declaring class so two tuples of
	// the same simple name in different classes of one script stay distinct. `tuple_name` is only
	// the display name. Empty for an unnamed (structural) tuple.
	if (p_tuple_name != StringName()) {
		type.native_type = p_owner_fqcn.is_empty() ? String(p_tuple_name) : p_owner_fqcn + ENUM_SEPARATOR + String(p_tuple_name);
	}
	type.script_path = p_script_path;
	type.container_element_types = p_element_types;
	type.tuple_field_names = p_field_names;
	type.is_meta_type = p_meta;
	type.is_constant = p_meta;
	return type;
}

// In enum types, native_type is used to store the class (native or otherwise) that the enum belongs to.
// This disambiguates between similarly named enums in base classes or outer classes
static FSParser::DataType make_enum_type(const StringName &p_enum_name, const String &p_base_name, const bool p_meta = false) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::ENUM;
	type.builtin_type = p_meta ? Variant::DICTIONARY : Variant::INT;
	type.enum_type = p_enum_name;
	type.is_constant = true;
	type.is_meta_type = p_meta;

	// For enums, native_type is only used to check compatibility in is_type_compatible()
	// We can set anything readable here for error messages, as long as it uniquely identifies the type of the enum
	if (p_base_name.is_empty()) {
		type.native_type = p_enum_name;
	} else {
		type.native_type = p_base_name + ENUM_SEPARATOR + p_enum_name;
	}

	return type;
}

static FSParser::DataType make_class_enum_type(const StringName &p_enum_name, FSParser::ClassNode *p_class, const String &p_script_path, bool p_meta = true) {
	FSParser::DataType type = make_enum_type(p_enum_name, p_class->fqcn, p_meta);

	type.class_type = p_class;
	type.script_path = p_script_path;

	return type;
}

static FSParser::DataType make_native_enum_type(const StringName &p_enum_name, const StringName &p_native_class, bool p_meta = true) {
	// Find out which base class declared the enum, so the name is always the same even when coming from other contexts.
	StringName native_base = p_native_class;
	while (true && native_base != StringName()) {
		if (ClassDB::has_enum(native_base, p_enum_name, true)) {
			break;
		}
		native_base = ClassDB::get_parent_class_nocheck(native_base);
	}

	FSParser::DataType type = make_enum_type(p_enum_name, native_base, p_meta);
	if (p_meta) {
		// Native enum types are not dictionaries.
		type.builtin_type = Variant::NIL;
		type.is_pseudo_type = true;
	}

	List<StringName> enum_values;
	ClassDB::get_enum_constants(native_base, p_enum_name, &enum_values, true);

	for (const StringName &E : enum_values) {
		type.enum_values[E] = ClassDB::get_integer_constant(native_base, E);
	}

	return type;
}

static FSParser::DataType make_builtin_enum_type(const StringName &p_enum_name, Variant::Type p_type, bool p_meta = true) {
	FSParser::DataType type = make_enum_type(p_enum_name, Variant::get_type_name(p_type), p_meta);
	if (p_meta) {
		// Built-in enum types are not dictionaries.
		type.builtin_type = Variant::NIL;
		type.is_pseudo_type = true;
	}

	List<StringName> enum_values;
	Variant::get_enumerations_for_enum(p_type, p_enum_name, &enum_values);

	for (const StringName &E : enum_values) {
		type.enum_values[E] = Variant::get_enum_value(p_type, p_enum_name, E);
	}

	return type;
}

static FSParser::DataType make_global_enum_type(const StringName &p_enum_name, const StringName &p_base, bool p_meta = true) {
	FSParser::DataType type = make_enum_type(p_enum_name, p_base, p_meta);
	if (p_meta) {
		// Global enum types are not dictionaries.
		type.builtin_type = Variant::NIL;
		type.is_pseudo_type = true;
	}

	HashMap<StringName, int64_t> enum_values;
	CoreConstants::get_enum_values(type.native_type, &enum_values);
	for (const KeyValue<StringName, int64_t> &element : enum_values) {
		type.enum_values[element.key] = element.value;
	}

	return type;
}

static FSParser::DataType make_standalone_global_enum_type(const StringName &p_global_name, bool p_meta = true) {
	FSParser::DataType type = make_enum_type(p_global_name, String(), p_meta);
	type.enum_type = p_global_name;
	type.native_type = p_global_name;
	return type;
}

static Dictionary make_enum_dictionary_from_type(const FSParser::DataType &p_type) {
	if (p_type.class_type != nullptr && p_type.class_type->enum_file_decl != nullptr) {
		return p_type.class_type->enum_file_decl->dictionary;
	}

	Dictionary dictionary;
	for (const KeyValue<StringName, int64_t> &element : p_type.enum_values) {
		dictionary[String(element.key)] = element.value;
	}
	dictionary.make_read_only();
	return dictionary;
}

static void set_enum_meta_identifier_constant(FSParser::IdentifierNode *p_identifier, const FSParser::DataType &p_type) {
	p_identifier->set_datatype(p_type);
	if (p_type.kind == FSParser::DataType::ENUM && p_type.is_meta_type) {
		p_identifier->is_constant = true;
		p_identifier->reduced_value = make_enum_dictionary_from_type(p_type);
	}
}

static FSParser::DataType make_builtin_meta_type(Variant::Type p_type) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::BUILTIN;
	type.builtin_type = p_type;
	type.is_constant = true;
	type.is_meta_type = true;
	return type;
}

FSParser::DataType FSAnalyzer::resolve_datatype(FSParser::TypeNode *p_type) {
	FSParser::DataType bad_type;
	bad_type.kind = FSParser::DataType::VARIANT;
	bad_type.type_source = FSParser::DataType::INFERRED;

	if (p_type == nullptr) {
		return bad_type;
	}

	if (p_type->get_datatype().is_resolving()) {
		push_error(R"(Could not resolve datatype: Cyclic reference.)", p_type);
		return bad_type;
	}

	if (!p_type->get_datatype().has_no_type()) {
		return p_type->get_datatype();
	}

	FSParser::DataType resolving_datatype;
	resolving_datatype.kind = FSParser::DataType::RESOLVING;
	p_type->set_datatype(resolving_datatype);

	FSParser::DataType result;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	auto finalize_datatype = [&](FSParser::DataType p_result) -> FSParser::DataType {
		if (p_type->is_nullable && !p_result.is_variant() && !(p_result.kind == FSParser::DataType::BUILTIN && p_result.builtin_type == Variant::NIL)) {
			p_result.is_nullable = true;
		}
		p_type->set_datatype(p_result);
		return p_result;
	};
	auto reject_nested_type_handle = [&](const FSParser::DataType &p_nested_type, FSParser::TypeNode *p_nested_node) -> bool {
		if (p_nested_type.is_type_handle_annotation) {
			push_error("Type[T] cannot be used as a nested type argument yet.", p_nested_node);
			return true;
		}
		return false;
	};

	if (p_type->is_tuple) {
		// An unnamed tuple type (`(int, String)`) is structural: its identity is exactly the
		// element list, so it carries no name and no field names.
		Vector<FSParser::DataType> element_types;
		for (int i = 0; i < p_type->tuple_element_types.size(); i++) {
			element_types.push_back(type_from_metatype(resolve_datatype(p_type->tuple_element_types[i])));
		}
		result = make_tuple_type(StringName(), String(), String(), element_types, Vector<StringName>(), false);
		return finalize_datatype(result);
	}

	if (p_type->type_chain.is_empty()) {
		// void.
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::NIL;
		return finalize_datatype(result);
	}

	const FSParser::IdentifierNode *first_id = p_type->type_chain[0];
	StringName first = first_id->name;
	bool type_found = false;
	int resolved_type_chain_size = 1;

	// Type parameters of the enclosing generic class or method shadow any other name, so a bare
	// `T` resolves to the parameter handle before falling back to the normal type lookup below.
	{
		FSParser::DataType type_parameter;
		if (resolve_type_parameter(first, type_parameter)) {
			if (p_type->type_chain.size() > 1) {
				push_error(vformat(R"(Type parameter "%s" does not contain nested types.)", first), p_type->type_chain[1]);
				return bad_type;
			}
			if (!p_type->container_types.is_empty()) {
				push_error(vformat(R"(Type parameter "%s" cannot be specialized with type arguments.)", first), p_type);
				return bad_type;
			}
			return finalize_datatype(type_parameter);
		}
	}

	if (first == SNAME("Self") && parser->current_class != nullptr) {
		if (p_type->type_chain.size() > 1) {
			push_error(R"(Type "Self" does not contain nested types.)", p_type->type_chain[1]);
			return bad_type;
		}
		if (!p_type->container_types.is_empty()) {
			push_error(R"(Type "Self" cannot be specialized with type arguments.)", p_type);
			return bad_type;
		}

		FSParser::DataType enum_type = enum_self_type(parser->current_function);
		if (enum_type.is_set()) {
			return finalize_datatype(enum_type);
		}

		const bool receiver_relative_self = resolving_function_signature_type || parser->current_function != nullptr || parser->current_class->is_trait;
		FSParser::DataType self_type;
		if (receiver_relative_self) {
			if (parser->current_function != nullptr) {
				parser->current_function->uses_receiver_relative_self = true;
			}
			self_type = _self_type_parameter_for_class(parser->current_class);
		} else {
			self_type = _self_type_for_class(parser->current_class);
			self_type.is_meta_type = true;
		}
		return finalize_datatype(self_type);
	}

	if (first_id->suite && first_id->suite->has_local(first)) {
		const FSParser::SuiteNode::Local &local = first_id->suite->get_local(first);
		if (local.type == FSParser::SuiteNode::Local::CONSTANT) {
			result = local.get_datatype();
			if (!result.is_set()) {
				// Don't try to resolve it as the constant can be declared below.
				push_error(vformat(R"(Local constant "%s" is not resolved at this point.)", first), first_id);
				return bad_type;
			}
			if (result.is_meta_type) {
				type_found = true;
			} else if (Ref<Script>(local.constant->initializer->reduced_value).is_valid()) {
				Ref<FoundryScript> foundry_script = local.constant->initializer->reduced_value;
				if (foundry_script.is_valid()) {
					Ref<FSParserRef> ref = dependency_parser_access.depended_parser_for(foundry_script->get_script_path(), FSParserRef::INHERITANCE_SOLVED);
					if (ref.is_null() || ref->get_status() < FSParserRef::INHERITANCE_SOLVED) {
						push_error(vformat(R"(Could not parse script from "%s".)", foundry_script->get_script_path()), first_id);
						return bad_type;
					}
					result = ref->get_parser()->head->get_datatype();
				} else {
					result = make_script_meta_type(local.constant->initializer->reduced_value);
				}
				type_found = true;
			} else {
				push_error(vformat(R"(Local constant "%s" is not a valid type.)", first), first_id);
				return bad_type;
			}
		} else {
			push_error(vformat(R"(Local %s "%s" cannot be used as a type.)", local.get_name(), first), first_id);
			return bad_type;
		}
	}

	if (!type_found) {
		if (first == SNAME("Variant")) {
			if (p_type->type_chain.size() == 2) {
				// May be nested enum.
				const StringName enum_name = p_type->type_chain[1]->name;
				const StringName qualified_name = String(first) + ENUM_SEPARATOR + String(p_type->type_chain[1]->name);
				if (CoreConstants::is_global_enum(qualified_name)) {
					result = make_global_enum_type(enum_name, first, true);
					return finalize_datatype(result);
				} else {
					push_error(vformat(R"(Name "%s" is not a nested type of "Variant".)", enum_name), p_type->type_chain[1]);
					return bad_type;
				}
			} else if (p_type->type_chain.size() > 2) {
				push_error(R"(Variant only contains enum types, which do not have nested types.)", p_type->type_chain[2]);
				return bad_type;
			}
			result.kind = FSParser::DataType::VARIANT;
		} else if (FSParser::get_builtin_type(first) < Variant::VARIANT_MAX || first == SNAME("AsyncCallable")) {
			// Built-in types. AsyncCallable is an async-marked alias of Callable.
			const bool is_async_callable = first == SNAME("AsyncCallable");
			const Variant::Type builtin_type = is_async_callable ? Variant::CALLABLE : FSParser::get_builtin_type(first);

			if (p_type->type_chain.size() == 2) {
				// May be nested enum.
				const StringName enum_name = p_type->type_chain[1]->name;
				if (Variant::has_enum(builtin_type, enum_name)) {
					result = make_builtin_enum_type(enum_name, builtin_type, true);
					return finalize_datatype(result);
				} else {
					push_error(vformat(R"(Name "%s" is not a nested type of "%s".)", enum_name, first), p_type->type_chain[1]);
					return bad_type;
				}
			} else if (p_type->type_chain.size() > 2) {
				push_error(R"(Built-in types only contain enum types, which do not have nested types.)", p_type->type_chain[2]);
				return bad_type;
			}

			result.kind = FSParser::DataType::BUILTIN;
			result.builtin_type = builtin_type;

			if (builtin_type == Variant::CALLABLE || builtin_type == Variant::SIGNAL) {
				result.signature_is_async = is_async_callable;
				if (p_type->has_signature) {
					result.has_method_signature = true;
					result.has_explicit_method_signature = true;
					MethodInfo method_info;
					for (int i = 0; i < p_type->signature_parameter_types.size(); i++) {
						FSParser::DataType parameter_type = type_from_metatype(resolve_datatype(p_type->signature_parameter_types[i]));
						if (reject_nested_type_handle(parameter_type, p_type->signature_parameter_types[i])) {
							return bad_type;
						}
						result.method_parameter_types.push_back(parameter_type);
						method_info.arguments.push_back(parameter_type.to_property_info(""));
					}
					if (builtin_type == Variant::CALLABLE) {
						FSParser::DataType return_type = type_from_metatype(resolve_datatype(p_type->signature_return_type));
						if (reject_nested_type_handle(return_type, p_type->signature_return_type)) {
							return bad_type;
						}
						result.method_return_type.push_back(return_type);
						method_info.return_val = return_type.to_property_info("");
					}
					result.method_info = method_info;
				}
			}
			if (builtin_type == Variant::ARRAY) {
				FSParser::DataType container_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(0)));
				if (reject_nested_type_handle(container_type, p_type->get_container_type_or_null(0))) {
					return bad_type;
				}
				if (container_type.kind != FSParser::DataType::VARIANT) {
					container_type.is_constant = false;
					result.set_container_element_type(0, container_type);
				}
			}
			if (builtin_type == Variant::DICTIONARY) {
				FSParser::DataType key_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(0)));
				if (reject_nested_type_handle(key_type, p_type->get_container_type_or_null(0))) {
					return bad_type;
				}
				if (key_type.kind != FSParser::DataType::VARIANT) {
					key_type.is_constant = false;
					result.set_container_element_type(0, key_type);
				}
				FSParser::DataType value_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(1)));
				if (reject_nested_type_handle(value_type, p_type->get_container_type_or_null(1))) {
					return bad_type;
				}
				if (value_type.kind != FSParser::DataType::VARIANT) {
					value_type.is_constant = false;
					result.set_container_element_type(1, value_type);
				}
			}
		} else if (class_exists(first)) {
			// Native engine classes.
			result.kind = FSParser::DataType::NATIVE;
			result.builtin_type = Variant::OBJECT;
			result.native_type = first;
		} else {
			bool current_scope_has_name = false;
			List<FSParser::ClassNode *> script_classes;
			get_class_node_current_scope_classes(parser->current_class, &script_classes, p_type);
			for (FSParser::ClassNode *script_class : script_classes) {
				if ((script_class->identifier && script_class->identifier->name == first) || script_class->members_indices.has(first)) {
					current_scope_has_name = true;
					break;
				}
			}

			if (!current_scope_has_name) {
				StringName namespace_global_class;
				bool namespace_error = false;
				int namespace_type_chain_size = 0;
				if (get_namespace_global_class_from_type_chain(p_type->type_chain, p_type, namespace_global_class, namespace_type_chain_size, namespace_error)) {
					if (namespace_error) {
						return bad_type;
					}
					if (reject_bootstrap_global_class_dependency(namespace_global_class, p_type, "global type")) {
						return bad_type;
					}
					if (ScriptServer::is_global_class_enum(namespace_global_class)) {
						const String path = ScriptServer::get_global_class_path(namespace_global_class);
						result = make_global_enum_type_from_path(namespace_global_class, path, p_type);
					} else {
						result = make_global_class_meta_type(namespace_global_class, p_type);
					}
					resolved_type_chain_size = namespace_type_chain_size;
				}
			}
		}

		if (result.is_set()) {
			// Found.
		} else if (ScriptServer::is_global_class(first)) {
			if (reject_bootstrap_global_class_dependency(first, p_type, "global type")) {
				return bad_type;
			}
			if (ScriptServer::is_global_class_enum(first)) {
				const String path = ScriptServer::get_global_class_path(first);
				result = make_global_enum_type_from_path(first, path, p_type);
			} else {
				if (FoundryScript::is_canonically_equal_paths(parser->script_path, ScriptServer::get_global_class_path(first))) {
					// A `tuple_name` file naming itself is a by-value cycle; resolving through the
					// declaration (rather than the head class) is what reports it.
					result = parser->head != nullptr && parser->head->is_tuple_file
							? make_global_tuple_type_from_current_parser(first, p_type)
							: parser->head->get_datatype();
				} else {
					String path = ScriptServer::get_global_class_path(first);
					String ext = path.get_extension();
					if (ext == FSLanguage::get_singleton()->get_extension()) {
						Ref<FSParserRef> ref = dependency_parser_access.depended_parser_for(path, FSParserRef::INHERITANCE_SOLVED);
						if (ref.is_null() || ref->get_status() < FSParserRef::INHERITANCE_SOLVED) {
							push_error(vformat(R"(Could not parse global class "%s" from "%s".)", first, ScriptServer::get_global_class_path(first)), p_type);
							return bad_type;
						}
						FSParser::ClassNode *global_head = ref->get_parser()->head;
						if (global_head != nullptr && global_head->is_tuple_file) {
							result = ref->get_analyzer()->make_global_tuple_type_from_current_parser(first, global_head);
						} else {
							result = global_head->get_datatype();
						}
					} else {
						result = make_script_meta_type(ResourceLoader::load(path, "Script"));
					}
				}
			}
		} else if (ClassDB::has_enum(parser->current_class->base_type.native_type, first)) {
			// Native enum in current class.
			result = make_native_enum_type(first, parser->current_class->base_type.native_type);
		} else if (CoreConstants::is_global_enum(first)) {
			if (p_type->type_chain.size() > 1) {
				push_error(R"(Enums cannot contain nested types.)", p_type->type_chain[1]);
				return bad_type;
			}
			result = make_global_enum_type(first, StringName());
		} else {
			// Classes in current scope.
			List<FSParser::ClassNode *> script_classes;
			bool found = false;
			get_class_node_current_scope_classes(parser->current_class, &script_classes, p_type);
			for (FSParser::ClassNode *script_class : script_classes) {
				if (found) {
					break;
				}

				if (script_class->identifier && script_class->identifier->name == first) {
					result = script_class->get_datatype();
					break;
				}
				if (script_class->members_indices.has(first)) {
					resolve_class_member(script_class, first, p_type);

					FSParser::ClassNode::Member member = script_class->get_member(first);
					switch (member.type) {
						case FSParser::ClassNode::Member::CLASS:
							result = member.get_datatype();
							found = true;
							break;
						case FSParser::ClassNode::Member::ENUM:
							result = member.get_datatype();
							found = true;
							break;
						case FSParser::ClassNode::Member::TUPLE:
							result = member.get_datatype();
							if (result.is_resolving()) {
								push_error(vformat(R"(Tuple "%s" cannot contain itself by value.)", first), p_type);
								return bad_type;
							}
							found = true;
							break;
						case FSParser::ClassNode::Member::CONSTANT:
							if (member.get_datatype().is_meta_type) {
								result = member.get_datatype();
								found = true;
								break;
							} else if (Ref<Script>(member.constant->initializer->reduced_value).is_valid()) {
								Ref<FoundryScript> foundry_script = member.constant->initializer->reduced_value;
								if (foundry_script.is_valid()) {
									Ref<FSParserRef> ref = dependency_parser_access.depended_parser_for(foundry_script->get_script_path(), FSParserRef::INHERITANCE_SOLVED);
									if (ref.is_null() || ref->get_status() < FSParserRef::INHERITANCE_SOLVED) {
										push_error(vformat(R"(Could not parse script from "%s".)", foundry_script->get_script_path()), p_type);
										return bad_type;
									}
									result = ref->get_parser()->head->get_datatype();
								} else {
									result = make_script_meta_type(member.constant->initializer->reduced_value);
								}
								found = true;
								break;
							}
							[[fallthrough]];
						default:
							push_error(vformat(R"("%s" is a %s but does not contain a type.)", first, member.get_type_name()), p_type);
							return bad_type;
					}
				}
			}
		}
	}

	if (!result.is_set() && p_type->is_coroutine) {
		// Coroutine[T] is recognized in annotation position as a source-level skin over
		// FSFunctionState rather than as a real class. The parser guarantees the bracketed
		// form carries exactly one result type in container_types[0]; route it into the phantom
		// result slot via make_coroutine_type.
		if (p_type->type_chain.size() != 1 || first != SNAME("Coroutine") || p_type->container_types.size() != 1) {
			push_error("Coroutine[T] expects exactly one result type argument.", p_type);
			return bad_type;
		}
		FSParser::DataType result_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(0)));
		if (reject_nested_type_handle(result_type, p_type->get_container_type_or_null(0))) {
			return bad_type;
		}
		return finalize_datatype(make_coroutine_type(result_type));
	}

	if (!result.is_set() && first == SNAME("Type")) {
		if (p_type->type_chain.size() != 1 || p_type->container_types.size() != 1) {
			push_error("Type[T] expects exactly one type argument.", p_type);
			return bad_type;
		}

		FSParser::DataType represented_type = type_from_metatype(
				resolve_datatype(p_type->get_container_type_or_null(0)));
		if (represented_type.is_variant()) {
			push_error("Type[T] requires an object, script, class, trait, or type-parameter argument.", p_type);
			return bad_type;
		}
		if (represented_type.kind == FSParser::DataType::BUILTIN) {
			push_error(vformat(R"(Builtin metatypes such as "Type[%s]" are not supported yet.)",
							   represented_type.to_string()),
					p_type);
			return bad_type;
		}
		if (represented_type.kind == FSParser::DataType::ENUM || represented_type.is_type_handle_annotation) {
			push_error("Type[T] requires an object, script, class, trait, or type-parameter argument.", p_type);
			return bad_type;
		}

		represented_type.is_constant = true;
		represented_type.is_meta_type = true;
		represented_type.is_type_handle_annotation = true;
		return finalize_datatype(represented_type);
	}

	if (!result.is_set()) {
		push_error(vformat(R"(Could not find type "%s" in the current scope.)", first), p_type);
		return bad_type;
	}

	if (p_type->type_chain.size() > resolved_type_chain_size) {
		if (result.kind == FSParser::DataType::CLASS) {
			for (int i = resolved_type_chain_size; i < p_type->type_chain.size(); i++) {
				FSParser::DataType base = result;
				reduce_identifier_from_base(p_type->type_chain[i], &base);
				result = p_type->type_chain[i]->get_datatype();
				if (!result.is_set()) {
					push_error(vformat(R"(Could not find type "%s" under base "%s".)", p_type->type_chain[i]->name, base.to_string()), p_type->type_chain[resolved_type_chain_size]);
					return bad_type;
				} else if (!result.is_meta_type) {
					push_error(vformat(R"(Member "%s" under base "%s" is not a valid type.)", p_type->type_chain[i]->name, base.to_string()), p_type->type_chain[resolved_type_chain_size]);
					return bad_type;
				}
			}
		} else if (result.kind == FSParser::DataType::NATIVE) {
			// Only enums allowed for native.
			if (ClassDB::has_enum(result.native_type, p_type->type_chain[resolved_type_chain_size]->name)) {
				if (p_type->type_chain.size() > resolved_type_chain_size + 1) {
					push_error(R"(Enums cannot contain nested types.)", p_type->type_chain[resolved_type_chain_size + 1]);
					return bad_type;
				} else {
					result = make_native_enum_type(p_type->type_chain[resolved_type_chain_size]->name, result.native_type);
				}
			} else {
				push_error(vformat(R"(Could not find type "%s" in "%s".)", p_type->type_chain[resolved_type_chain_size]->name, first), p_type->type_chain[resolved_type_chain_size]);
				return bad_type;
			}
		} else {
			push_error(vformat(R"(Could not find nested type "%s" under base "%s".)", p_type->type_chain[resolved_type_chain_size]->name, result.to_string()), p_type->type_chain[resolved_type_chain_size]);
			return bad_type;
		}
	}

	if (!p_type->container_types.is_empty()) {
		if (result.builtin_type == Variant::ARRAY) {
			if (p_type->container_types.size() != 1) {
				push_error(R"(Typed arrays require exactly one collection element type.)", p_type);
				return bad_type;
			}
		} else if (result.builtin_type == Variant::DICTIONARY) {
			if (p_type->container_types.size() != 2) {
				push_error(R"(Typed dictionaries require exactly two collection element types.)", p_type);
				return bad_type;
			}
		} else if (result.kind == FSParser::DataType::CLASS && result.class_type != nullptr) {
			// Generic class specialization, e.g. `Box[int]`. The brackets carry type arguments
			// rather than collection element types, so they must match the class's parameter list.
			if (!apply_class_type_arguments(result, p_type->container_types, p_type)) {
				return bad_type;
			}
		} else {
			push_error(R"(Only arrays and dictionaries can specify collection element types.)", p_type);
			return bad_type;
		}
	}

	return finalize_datatype(result);
}

FSParser::DataType FSAnalyzer::substitute_member_type(
		const FSParser::DataType &p_member_type,
		const FSParser::DataType &p_base,
		const FSParser::FunctionNode *p_shadowing_method,
		const FSParser::DataType *p_self_type) {
	if (p_base.class_type == nullptr && p_self_type == nullptr) {
		return p_member_type;
	}

	HashMap<StringName, FSParser::DataType> bindings;
	if (p_base.has_type_arguments() && p_base.class_type != nullptr) {
		const Vector<FSParser::TypeParameterNode *> &type_parameters = p_base.class_type->type_parameters;
		const int binding_count = MIN(type_parameters.size(), p_base.type_arguments.size());
		for (int i = 0; i < binding_count; i++) {
			const FSParser::TypeParameterNode *parameter = type_parameters[i];
			if (parameter != nullptr && parameter->identifier != nullptr) {
				bindings.insert(parameter->identifier->name, p_base.type_arguments[i]);
			}
		}
	}
	if (p_self_type != nullptr && p_self_type->is_set()) {
		bindings.insert(SNAME("@Self"), *p_self_type);
	} else if (p_base.class_type != nullptr) {
		bindings.insert(SNAME("@Self"), type_handle_represented_type(p_base));
	}
	// A method's own type parameters shadow same-named class parameters within its signature, so the
	// class specialization must not rewrite them (e.g. `func echo[T](v: T)` on a `Box[int]`).
	if (p_shadowing_method != nullptr) {
		for (const FSParser::TypeParameterNode *parameter : p_shadowing_method->type_parameters) {
			if (parameter != nullptr && parameter->identifier != nullptr) {
				bindings.erase(parameter->identifier->name);
			}
		}
	}
	if (bindings.is_empty()) {
		return p_member_type;
	}
	return FSParser::DataType::substitute(p_member_type, bindings);
}

static FSParser::DataType _substitute_self_type_parameter(
		const FSParser::DataType &p_type,
		const FSParser::DataType &p_self_type) {
	if (!p_self_type.is_set()) {
		return p_type;
	}
	HashMap<StringName, FSParser::DataType> bindings;
	bindings.insert(SNAME("@Self"), p_self_type);
	return FSParser::DataType::substitute(p_type, bindings);
}

static FSParser::DataType _substitute_type_parameters_and_self(
		const FSParser::DataType &p_type,
		const HashMap<StringName, FSParser::DataType> &p_bindings,
		const FSParser::DataType &p_self_type) {
	return _substitute_self_type_parameter(FSParser::DataType::substitute(p_type, p_bindings), p_self_type);
}

void FSAnalyzer::resolve_class_body(FSParser::ClassNode *p_class, const FSParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<FSParserRef> parser_ref = dependency_parser_access.ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class body", p_source);

	if (p_class->resolved_body) {
		return;
	}

	if (!parser->has_class(p_class)) {
		if (parser_ref.is_null()) {
			// Error already pushed.
			return;
		}

		Error err = dependency_parser_access.raise_parser_to_status(parser_ref, FSParserRef::PARSED);
		if (err) {
			push_error(vformat(R"(Could not parse script "%s": %s.)", p_class->get_datatype().script_path, error_names[err]), p_source);
			return;
		}

		FSAnalyzer *other_analyzer = parser_ref->get_analyzer();
		FSParser *other_parser = parser_ref->get_parser();

		int error_count = other_parser->errors.size();
		other_analyzer->resolve_class_body(p_class);
		if (other_parser->errors.size() > error_count) {
			push_error(vformat(R"(Could not resolve class "%s".)", p_class->fqcn), p_source);
			return;
		}

		return;
	}

	p_class->resolved_body = true;

	FSParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;

	resolve_class_interface(p_class, p_source);

	// A class flattens its applied traits' members — including method bodies — into
	// itself at compile time. Identifiers inside a trait body only get their source
	// resolved when that trait is fully solved, so external traits must be raised to
	// `FULLY_SOLVED` here before the implementer's own body (and later the compiler)
	// flattens them. Only the directly applied traits are raised: a trait reached
	// transitively is fully solved by the body resolution of the trait that applies it
	// (reachable from that trait's parser, not necessarily this one). Inline traits are
	// solved as members of this same parser.
	for (const FSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		FSParser::ClassNode *trait = trait_use.resolved_trait;
		if (trait == nullptr) {
			continue;
		}
		Ref<FSParserRef> trait_parser_ref = dependency_parser_access.ensure_cached_external_parser_for_class(trait, p_class, "Trying to resolve trait body for flattening", p_source);
		if (trait_parser_ref.is_valid()) {
			Error err = dependency_parser_access.raise_parser_to_status(trait_parser_ref, FSParserRef::FULLY_SOLVED);
			if (err != OK) {
				push_error(vformat(R"(Could not resolve body of trait "%s" applied by "%s".)", _class_or_trait_name(trait), _class_or_trait_name(p_class)), p_source);
			}
		}
	}

	FSParser::DataType base_type = p_class->base_type;
	if (base_type.kind == FSParser::DataType::CLASS) {
		FSParser::ClassNode *base_class = base_type.class_type;
		resolve_class_body(base_class, p_class);
	}

	if (p_class == parser->head && p_class->is_enum_file && p_class->enum_file_decl != nullptr) {
		resolve_enum_bodies(p_class->enum_file_decl, p_class);
	}

	// Do functions, properties, and groups now.
	for (int i = 0; i < p_class->members.size(); i++) {
		FSParser::ClassNode::Member member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			// Apply annotations.
			for (FSParser::AnnotationNode *&E : member.function->annotations) {
				resolve_annotation(E, FSParser::AnnotationDeclarationNode::TARGET_METHOD);
				E->apply(parser, member.function, p_class);
			}
			resolve_function_body(member.function);
		} else if (member.type == FSParser::ClassNode::Member::ENUM) {
			resolve_enum_bodies(member.m_enum, p_class);
		} else if (member.type == FSParser::ClassNode::Member::VARIABLE && member.variable->property != FSParser::VariableNode::PROP_NONE) {
			if (member.variable->property == FSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					member.variable->getter->return_type = member.variable->datatype_specifier;
					member.variable->getter->set_datatype(member.get_datatype());

					resolve_function_body(member.variable->getter);
				}
				if (member.variable->setter != nullptr) {
					ERR_CONTINUE(member.variable->setter->parameters.is_empty());
					member.variable->setter->parameters[0]->datatype_specifier = member.variable->datatype_specifier;
					member.variable->setter->parameters[0]->set_datatype(member.get_datatype());

					resolve_function_body(member.variable->setter);
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::GROUP) {
			// Apply annotation (`@export_{category,group,subgroup}`).
			resolve_annotation(member.annotation);
			member.annotation->apply(parser, nullptr, p_class);
		}
	}

	// Check unused variables and datatypes of property getters and setters.
	for (int i = 0; i < p_class->members.size(); i++) {
		FSParser::ClassNode::Member member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::VARIABLE) {
#ifdef DEBUG_ENABLED
			if (member.variable->usages == 0 && String(member.variable->identifier->name).begins_with("_")) {
				parser->push_warning(member.variable->identifier, FSWarning::UNUSED_PRIVATE_CLASS_VARIABLE, member.variable->identifier->name);
			}
#endif // DEBUG_ENABLED

			if (member.variable->property == FSParser::VariableNode::PROP_SETGET) {
				FSParser::FunctionNode *getter_function = nullptr;
				FSParser::FunctionNode *setter_function = nullptr;

				bool has_valid_getter = false;
				bool has_valid_setter = false;

				if (member.variable->getter_pointer != nullptr) {
					if (p_class->has_function(member.variable->getter_pointer->name)) {
						getter_function = p_class->get_member(member.variable->getter_pointer->name).function;
					}

					if (getter_function == nullptr) {
						push_error(vformat(R"(Getter "%s" not found.)", member.variable->getter_pointer->name), member.variable);
					} else {
						FSParser::DataType return_datatype = getter_function->datatype;
						if (getter_function->return_type != nullptr) {
							return_datatype = getter_function->return_type->datatype;
							return_datatype.is_meta_type = false;
						}

						if (getter_function->parameters.size() != 0 || return_datatype.has_no_type()) {
							push_error(vformat(R"(Function "%s" cannot be used as getter because of its signature.)", getter_function->identifier->name), member.variable);
						} else if (!is_type_compatible(member.variable->datatype, return_datatype, true)) {
							push_error(vformat(R"(Function with return type "%s" cannot be used as getter for a property of type "%s".)", return_datatype.to_string(), member.variable->datatype.to_string()), member.variable);

						} else {
							has_valid_getter = true;
#ifdef DEBUG_ENABLED
							if (member.variable->datatype.builtin_type == Variant::INT && return_datatype.builtin_type == Variant::FLOAT) {
								parser->push_warning(member.variable, FSWarning::NARROWING_CONVERSION);
							}
#endif // DEBUG_ENABLED
						}
					}
				}

				if (member.variable->setter_pointer != nullptr) {
					if (p_class->has_function(member.variable->setter_pointer->name)) {
						setter_function = p_class->get_member(member.variable->setter_pointer->name).function;
					}

					if (setter_function == nullptr) {
						push_error(vformat(R"(Setter "%s" not found.)", member.variable->setter_pointer->name), member.variable);

					} else if (setter_function->parameters.size() != 1) {
						push_error(vformat(R"(Function "%s" cannot be used as setter because of its signature.)", setter_function->identifier->name), member.variable);

					} else if (!is_type_compatible(member.variable->datatype, setter_function->parameters[0]->datatype, true)) {
						push_error(vformat(R"(Function with argument type "%s" cannot be used as setter for a property of type "%s".)", setter_function->parameters[0]->datatype.to_string(), member.variable->datatype.to_string()), member.variable);

					} else {
						has_valid_setter = true;

#ifdef DEBUG_ENABLED
						if (member.variable->datatype.builtin_type == Variant::FLOAT && setter_function->parameters[0]->datatype.builtin_type == Variant::INT) {
							parser->push_warning(member.variable, FSWarning::NARROWING_CONVERSION);
						}
#endif // DEBUG_ENABLED
					}
				}

				if (member.variable->datatype.is_variant() && has_valid_getter && has_valid_setter) {
					if (!is_type_compatible(getter_function->datatype, setter_function->parameters[0]->datatype, true)) {
						push_error(vformat(R"(Getter with type "%s" cannot be used along with setter of type "%s".)", getter_function->datatype.to_string(), setter_function->parameters[0]->datatype.to_string()), member.variable);
					}
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::SIGNAL) {
#ifdef DEBUG_ENABLED
			if (member.signal->usages == 0) {
				parser->push_warning(member.signal->identifier, FSWarning::UNUSED_SIGNAL, member.signal->identifier->name);
			}
#endif // DEBUG_ENABLED
		}
	}

	if (!pending_body_resolution_lambdas.is_empty()) {
		ERR_PRINT("FoundryScript bug (please report): Not all pending lambda bodies were resolved in time.");
		resolve_pending_lambda_bodies();
	}

	// Resolve base abstract class/method implementation requirements.
	if (!p_class->is_abstract && !p_class->is_trait) {
		HashSet<StringName> implemented_funcs;
		const FSParser::ClassNode *base_class = p_class;
		while (base_class != nullptr) {
			if (!base_class->is_abstract && base_class != p_class) {
				break;
			}
			for (FSParser::ClassNode::Member member : base_class->members) {
				if (member.type == FSParser::ClassNode::Member::FUNCTION) {
					if (member.function->is_abstract) {
						if (base_class == p_class) {
							const String class_name = p_class->identifier == nullptr ? p_class->fqcn.get_file() : String(p_class->identifier->name);
							push_error(vformat(R"*(Class "%s" is not abstract but contains abstract methods. Mark the class as "abstract" or remove "abstract" from all methods in this class.)*", class_name), p_class);
							break;
						} else if (!implemented_funcs.has(member.function->identifier->name)) {
							const String class_name = p_class->identifier == nullptr ? p_class->fqcn.get_file() : String(p_class->identifier->name);
							const String base_class_name = base_class->identifier == nullptr ? base_class->fqcn.get_file() : String(base_class->identifier->name);
							push_error(vformat(R"*(Class "%s" must implement "%s.%s()" and other inherited abstract methods or be marked as "abstract".)*", class_name, base_class_name, member.function->identifier->name), p_class);
							break;
						}
					} else {
						implemented_funcs.insert(member.function->identifier->name);
					}
				}
			}
			if (base_class->base_type.kind == FSParser::DataType::CLASS) {
				base_class = base_class->base_type.class_type;
			} else if (base_class->base_type.kind == FSParser::DataType::SCRIPT) {
				Ref<FSParserRef> base_parser_ref = parser->get_depended_parser_for(base_class->base_type.script_path);
				ERR_BREAK(base_parser_ref.is_null());
				base_class = base_parser_ref->get_parser()->head;
			} else {
				break;
			}
		}
	}

	mark_analyzer_phase_completed(AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL);
	run_phase_flow_finality_invariants(p_class);

	parser->current_class = previous_class;
}

void FSAnalyzer::resolve_enum_bodies(FSParser::EnumNode *p_enum, FSParser::ClassNode *p_owner) {
	ERR_FAIL_NULL(p_enum);
	ERR_FAIL_NULL(p_owner);

	AnalysisScopeGuard scope(this, p_owner);
	current_enum = p_enum;
	for (FSParser::FunctionNode *function : p_enum->functions) {
		if (function != nullptr) {
			resolve_function_body(function);
		}
	}
}

void FSAnalyzer::resolve_class_body(FSParser::ClassNode *p_class, bool p_recursive) {
	resolve_class_body(p_class);

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			FSParser::ClassNode::Member member = p_class->members[i];
			if (member.type == FSParser::ClassNode::Member::CLASS) {
				resolve_class_body(member.m_class, true);
			}
		}
	}
}

void FSAnalyzer::resolve_node(FSParser::Node *p_node, bool p_is_root) {
	ERR_FAIL_NULL_MSG(p_node, "Trying to resolve type of a null node.");

	switch (p_node->type) {
		case FSParser::Node::NONE:
			break; // Unreachable.
		case FSParser::Node::CLASS:
			// NOTE: Currently this route is never executed, `resolve_class_*()` is called directly.
			if (OK == resolve_class_inheritance(static_cast<FSParser::ClassNode *>(p_node), true)) {
				resolve_class_interface(static_cast<FSParser::ClassNode *>(p_node), true);
				resolve_class_body(static_cast<FSParser::ClassNode *>(p_node), true);
			}
			break;
		case FSParser::Node::CONSTANT:
			resolve_constant(static_cast<FSParser::ConstantNode *>(p_node), true);
			break;
		case FSParser::Node::FOR:
			resolve_for(static_cast<FSParser::ForNode *>(p_node));
			break;
		case FSParser::Node::IF:
			resolve_if(static_cast<FSParser::IfNode *>(p_node));
			break;
		case FSParser::Node::SUITE:
			resolve_suite(static_cast<FSParser::SuiteNode *>(p_node));
			break;
		case FSParser::Node::VARIABLE:
			resolve_variable(static_cast<FSParser::VariableNode *>(p_node), true);
			break;
		case FSParser::Node::VARIABLE_DESTRUCTURE:
			resolve_variable_destructure(static_cast<FSParser::VariableDestructureNode *>(p_node));
			break;
		case FSParser::Node::WHILE:
			resolve_while(static_cast<FSParser::WhileNode *>(p_node));
			break;
		case FSParser::Node::ANNOTATION:
			resolve_annotation(static_cast<FSParser::AnnotationNode *>(p_node));
			break;
		case FSParser::Node::ASSERT:
			resolve_assert(static_cast<FSParser::AssertNode *>(p_node));
			break;
		case FSParser::Node::MATCH:
			resolve_match(static_cast<FSParser::MatchNode *>(p_node));
			break;
		case FSParser::Node::MATCH_BRANCH:
			resolve_match_branch(static_cast<FSParser::MatchBranchNode *>(p_node), nullptr);
			break;
		case FSParser::Node::PARAMETER:
			resolve_parameter(static_cast<FSParser::ParameterNode *>(p_node));
			break;
		case FSParser::Node::PATTERN:
			resolve_match_pattern(static_cast<FSParser::PatternNode *>(p_node), nullptr);
			break;
		case FSParser::Node::RETURN:
			resolve_return(static_cast<FSParser::ReturnNode *>(p_node));
			break;
		case FSParser::Node::TYPE:
			resolve_datatype(static_cast<FSParser::TypeNode *>(p_node));
			break;
		// Resolving expression is the same as reducing them.
		case FSParser::Node::ARRAY:
		case FSParser::Node::ASSIGNMENT:
		case FSParser::Node::AWAIT:
		case FSParser::Node::BINARY_OPERATOR:
		case FSParser::Node::CALL:
		case FSParser::Node::CAST:
		case FSParser::Node::DICTIONARY:
		case FSParser::Node::GET_NODE:
		case FSParser::Node::IDENTIFIER:
		case FSParser::Node::LAMBDA:
		case FSParser::Node::LITERAL:
		case FSParser::Node::PRELOAD:
		case FSParser::Node::SELF:
		case FSParser::Node::SUBSCRIPT:
		case FSParser::Node::TERNARY_OPERATOR:
		case FSParser::Node::TUPLE_LITERAL:
		case FSParser::Node::TYPE_TEST:
		case FSParser::Node::UNARY_OPERATOR:
			reduce_expression(static_cast<FSParser::ExpressionNode *>(p_node), p_is_root);
			break;
		case FSParser::Node::ANNOTATION_DECLARATION:
		case FSParser::Node::BREAK:
		case FSParser::Node::BREAKPOINT:
		case FSParser::Node::CONFORMANCE:
		case FSParser::Node::CONTINUE:
		case FSParser::Node::ENUM:
		case FSParser::Node::FUNCTION:
		case FSParser::Node::PASS:
		case FSParser::Node::SIGNAL:
		case FSParser::Node::TUPLE:
		case FSParser::Node::TYPE_PARAMETER:
			// Nothing to do. Custom annotation declarations are resolved in a later pass.
			break;
	}
}

void FSAnalyzer::resolve_annotation(FSParser::AnnotationNode *p_annotation, uint32_t p_target_kind) {
	if (p_annotation->name == SNAME("@autoload")) {
		resolve_autoload_annotation(p_annotation);
		return;
	}

	if (p_annotation->is_custom) {
		// Unresolved custom annotation usage: resolve it against same-namespace or imported
		// annotation declarations and validate its target and arguments.
		resolve_custom_annotation(p_annotation, p_target_kind);
		return;
	}

	ERR_FAIL_COND_MSG(!parser->valid_annotations.has(p_annotation->name), vformat(R"(Annotation "%s" not found to validate.)", p_annotation->name));

	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;

	const MethodInfo &annotation_info = parser->valid_annotations[p_annotation->name].info;

	for (int64_t i = 0, j = 0; i < p_annotation->arguments.size(); i++) {
		FSParser::ExpressionNode *argument = p_annotation->arguments[i];
		const PropertyInfo &argument_info = annotation_info.arguments[j];

		if (j + 1 < annotation_info.arguments.size()) {
			++j;
		}

		reduce_expression(argument);

		if (!argument->is_constant) {
			push_error(vformat(R"(Argument %d of annotation "%s" isn't a constant expression.)", i + 1, p_annotation->name), argument);
			return;
		}

		Variant value = argument->reduced_value;

		if (value.get_type() != argument_info.type) {
#ifdef DEBUG_ENABLED
			if (argument_info.type == Variant::INT && value.get_type() == Variant::FLOAT) {
				parser->push_warning(argument, FSWarning::NARROWING_CONVERSION);
			}
#endif // DEBUG_ENABLED

			if (!Variant::can_convert_strict(value.get_type(), argument_info.type)) {
				push_error(vformat(R"(Invalid argument for annotation "%s": argument %d should be "%s" but is "%s".)", p_annotation->name, i + 1, Variant::get_type_name(argument_info.type), argument->get_datatype().to_string()), argument);
				return;
			}

			Variant converted_to;
			const Variant *converted_from = &value;
			Callable::CallError call_error;
			Variant::construct(argument_info.type, converted_to, &converted_from, 1, call_error);

			if (call_error.error != Callable::CallError::CALL_OK) {
				push_error(vformat(R"(Cannot convert argument %d of annotation "%s" from "%s" to "%s".)", i + 1, p_annotation->name, Variant::get_type_name(value.get_type()), Variant::get_type_name(argument_info.type)), argument);
				return;
			}

			value = converted_to;
		}

		p_annotation->resolved_arguments.push_back(value);
	}
}

bool FSAnalyzer::get_autoload_dependency_name_from_expression(
		FSParser::ExpressionNode *p_expression,
		StringName &r_name) {
	if (p_expression == nullptr) {
		return false;
	}

	const FSParser::DataType datatype = p_expression->get_datatype();
	if (datatype.is_meta_type && datatype.kind == FSParser::DataType::CLASS && datatype.class_type != nullptr) {
		r_name = datatype.class_type->get_global_name();
		return r_name != StringName();
	}

	if (datatype.is_meta_type && datatype.kind == FSParser::DataType::SCRIPT && datatype.script_type.is_valid()) {
		const String script_path = datatype.script_type->get_path();
		LocalVector<StringName> global_classes;
		ScriptServer::get_global_class_list(global_classes);
		for (const StringName &global_class : global_classes) {
			if (FoundryScript::is_canonically_equal_paths(ScriptServer::get_global_class_path(global_class), script_path)) {
				r_name = global_class;
				return true;
			}
		}
	}

	if (p_expression->type == FSParser::Node::IDENTIFIER) {
		const FSParser::IdentifierNode *identifier = static_cast<FSParser::IdentifierNode *>(p_expression);
		if (identifier->source != FSParser::IdentifierNode::UNDEFINED_SOURCE) {
			return false;
		}

		const StringName identifier_name = identifier->name;
		StringName global_class;
		bool global_class_error = false;
		if (get_global_class_in_namespace(parser->head->namespace_name, identifier_name, global_class) ||
				get_imported_global_class(identifier_name, p_expression, global_class, global_class_error)) {
			if (global_class_error) {
				return false;
			}
			r_name = global_class;
			return true;
		}

		if (ScriptServer::is_global_class(identifier_name)) {
			r_name = identifier_name;
			return true;
		}
	}

	return false;
}

void FSAnalyzer::resolve_autoload_annotation(FSParser::AnnotationNode *p_annotation) {
	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;

	bool valid = true;
	int autoload_annotation_count = 0;
	for (FSParser::AnnotationNode *annotation : parser->head->annotations) {
		if (annotation->name == SNAME("@autoload")) {
			autoload_annotation_count++;
		}
	}
	if (autoload_annotation_count > 1) {
		push_error(R"("@autoload" annotation can only be used once per script.)", p_annotation);
		valid = false;
	}

	FSAutoloadIndexEntry entry;
	entry.source = FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION;
	entry.path = parser->script_path;
	entry.script_path = parser->script_path;
	entry.is_singleton = true;
	entry.order = 0;
	entry.is_tool = parser->is_tool();

	if (parser->head->identifier == nullptr || parser->head->trait_name_used) {
		push_error(R"("@autoload" requires "class_name".)", p_annotation);
		valid = false;
	} else {
		entry.name = parser->head->identifier->name;
		entry.global_class_name = parser->head->get_global_name();
	}

	entry.native_base = parser->head->base_type.native_type;
	entry.is_node = entry.native_base != StringName() && ClassDB::is_parent_class(entry.native_base, SNAME("Node"));
	entry.is_same_script_global_class = entry.global_class_name == entry.name &&
			FoundryScript::is_canonically_equal_paths(entry.script_path, entry.path);
	if (!entry.is_node) {
		push_error(R"("@autoload" requires the script to inherit from "Node".)", p_annotation);
		valid = false;
	}

	bool bound_arguments[2] = { false, false };
	bool seen_named_argument = false;
	int next_positional_argument = 0;

	for (int i = 0; i < p_annotation->arguments.size(); i++) {
		FSParser::ExpressionNode *argument = p_annotation->arguments[i];
		const StringName argument_name = p_annotation->argument_names[i];

		int argument_index = -1;
		if (argument_name == StringName()) {
			if (seen_named_argument) {
				push_error(R"(Positional argument after named argument in annotation "@autoload".)", argument);
				valid = false;
				continue;
			}
			argument_index = next_positional_argument++;
		} else {
			seen_named_argument = true;
			if (argument_name == SNAME("depends_on")) {
				argument_index = 0;
			} else if (argument_name == SNAME("order_id")) {
				argument_index = 1;
			} else {
				push_error(vformat(R"(Annotation "@autoload" has no parameter named "%s".)", argument_name), argument);
				valid = false;
				continue;
			}
		}

		if (argument_index < 0 || argument_index >= 2) {
			push_error(vformat(R"(Annotation "@autoload" takes at most 2 argument(s), but %d were given.)",
							   p_annotation->arguments.size()),
					argument);
			valid = false;
			continue;
		}
		if (bound_arguments[argument_index]) {
			push_error(vformat(R"(Parameter "%s" of annotation "@autoload" was specified more than once.)",
							   argument_index == 0 ? "depends_on" : "order_id"),
					argument);
			valid = false;
			continue;
		}
		bound_arguments[argument_index] = true;

		reduce_expression(argument);
		if (argument_index == 0) {
			if (argument->type != FSParser::Node::ARRAY) {
				push_error(R"(Argument "depends_on" of annotation "@autoload" must be an array of class names.)", argument);
				valid = false;
				continue;
			}

			FSParser::ArrayNode *dependencies = static_cast<FSParser::ArrayNode *>(argument);
			for (int dependency_index = 0; dependency_index < dependencies->elements.size(); dependency_index++) {
				FSParser::ExpressionNode *dependency_expression = dependencies->elements[dependency_index];
				StringName dependency_name;
				if (!get_autoload_dependency_name_from_expression(dependency_expression, dependency_name)) {
					push_error(vformat(R"(Dependency %d of annotation "@autoload" must resolve to a script class.)",
									   dependency_index + 1),
							dependency_expression);
					valid = false;
					continue;
				}

				FSAutoloadIndexDependency dependency;
				dependency.name = dependency_name;
				dependency.is_autoload = true;
				entry.dependencies.push_back(dependency);
			}
		} else {
			if (!argument->is_constant || argument->reduced_value.get_type() != Variant::INT) {
				push_error(R"(Argument "order_id" of annotation "@autoload" must be a constant integer expression.)", argument);
				valid = false;
				continue;
			}
			int64_t order = argument->reduced_value.operator int64_t();
			if (order < INT_MIN || order > INT_MAX) {
				push_error(R"("order_id" of annotation "@autoload" must fit in a 32-bit signed integer.)", argument);
				valid = false;
				continue;
			}
			entry.order = static_cast<int>(order);
		}
	}

	if (!valid) {
		return;
	}

	Vector<FSAutoloadIndexEntry> entries = autoload_index.get_entries();
	entries.push_back(entry);
	autoload_index.rebuild_from_entries(entries);
}

static String _annotation_target_name(uint32_t p_target_kind) {
	switch (p_target_kind) {
		case FSParser::AnnotationDeclarationNode::TARGET_CLASS:
			return "a class";
		case FSParser::AnnotationDeclarationNode::TARGET_METHOD:
			return "a method";
		case FSParser::AnnotationDeclarationNode::TARGET_VARIABLE:
			return "a member variable";
		case FSParser::AnnotationDeclarationNode::TARGET_SIGNAL:
			return "a signal";
		case FSParser::AnnotationDeclarationNode::TARGET_CONSTANT:
			return "a constant";
		case FSParser::AnnotationDeclarationNode::TARGET_PARAMETER:
			return "a parameter";
		default:
			return "this target";
	}
}

bool FSAnalyzer::coerce_annotation_argument(const FSParser::DataType &p_parameter_type, Variant &r_value, const FSParser::ExpressionNode *p_argument, const String &p_context) {
	// Only concrete built-in parameter types drive a strict conversion. `Variant`, object, and
	// enum-typed parameters accept any constant value in v1, mirroring the permissive handling of
	// non-built-in built-in annotation arguments.
	if (p_parameter_type.kind != FSParser::DataType::BUILTIN) {
		return true;
	}
	const Variant::Type expected_type = p_parameter_type.builtin_type;
	if (expected_type == Variant::NIL || r_value.get_type() == expected_type) {
		return true;
	}

#ifdef DEBUG_ENABLED
	if (expected_type == Variant::INT && r_value.get_type() == Variant::FLOAT) {
		parser->push_warning(p_argument, FSWarning::NARROWING_CONVERSION);
	}
#endif // DEBUG_ENABLED

	if (!Variant::can_convert_strict(r_value.get_type(), expected_type)) {
		push_error(vformat(R"(Invalid %s: expected "%s" but got "%s".)", p_context, Variant::get_type_name(expected_type), Variant::get_type_name(r_value.get_type())), p_argument);
		return false;
	}

	Variant converted_value;
	const Variant *converted_from = &r_value;
	Callable::CallError call_error;
	Variant::construct(expected_type, converted_value, &converted_from, 1, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		push_error(vformat(R"(Cannot convert %s from "%s" to "%s".)", p_context, Variant::get_type_name(r_value.get_type()), Variant::get_type_name(expected_type)), p_argument);
		return false;
	}

	r_value = converted_value;
	return true;
}

void FSAnalyzer::resolve_annotation_declaration(FSParser::AnnotationDeclarationNode *p_declaration) {
	if (p_declaration == nullptr || p_declaration->resolved_signature) {
		return;
	}
	p_declaration->resolved_signature = true;

	FSParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = parser->head;

	Vector<FSParser::ParameterNode *> parameters = p_declaration->parameters;
	if (p_declaration->rest_parameter != nullptr) {
		parameters.push_back(p_declaration->rest_parameter);
	}

	for (FSParser::ParameterNode *parameter : parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}

		if (parameter->datatype_specifier == nullptr) {
			push_error(vformat(R"(Annotation parameter "%s" must declare a type.)", parameter->identifier->name), parameter);
			FSParser::DataType variant_type;
			variant_type.kind = FSParser::DataType::VARIANT;
			variant_type.type_source = FSParser::DataType::INFERRED;
			parameter->set_datatype(variant_type);
		} else {
			parameter->set_datatype(type_from_metatype(resolve_datatype(parameter->datatype_specifier)));
		}

		if (parameter->initializer != nullptr) {
			reduce_expression(parameter->initializer);
			if (!parameter->initializer->is_constant) {
				push_error(vformat(R"(Default value for annotation parameter "%s" must be a constant expression.)", parameter->identifier->name), parameter->initializer);
			} else {
				Variant default_value = parameter->initializer->reduced_value;
				const String context = vformat(R"(default value of annotation parameter "%s")", parameter->identifier->name);
				coerce_annotation_argument(parameter->get_datatype(), default_value, parameter->initializer, context);
			}
		}
	}

	parser->current_class = previous_class;
}

void FSAnalyzer::resolve_annotation_declaration_signatures() {
	for (FSParser::AnnotationDeclarationNode *declaration : parser->head->annotation_declarations) {
		resolve_annotation_declaration(declaration);
	}
}

FSParser::AnnotationDeclarationNode *FSAnalyzer::load_external_annotation_declaration(const String &p_qualified_name, FSParser::AnnotationNode *p_annotation, bool &r_error_reported) {
	r_error_reported = false;
	FSLanguage *language = FSLanguage::get_singleton();
	if (language == nullptr) {
		return nullptr;
	}

	const String path = language->get_global_annotation_path(StringName(p_qualified_name));
	if (path.is_empty() || FoundryScript::is_canonically_equal_paths(path, parser->script_path)) {
		// Either unknown or declared by this file, whose local declarations were already searched.
		return nullptr;
	}
	if (!is_bootstrap_dependency_path_allowed(path)) {
		push_error(vformat(R"(Build task bootstrap cannot use annotation "%s" from "%s"; it is outside the provider bootstrap root "%s".)",
						   p_qualified_name, path, bootstrap_allowed_dependency_root),
				p_annotation);
		r_error_reported = true;
		return nullptr;
	}

	Ref<FSParserRef> ref = dependency_parser_access.depended_parser_for(path, FSParserRef::INTERFACE_SOLVED);
	if (ref.is_null() || ref->get_status() < FSParserRef::INTERFACE_SOLVED) {
		return nullptr;
	}

	FSParser *external_parser = ref->get_parser();
	if (external_parser == nullptr || external_parser->head == nullptr) {
		return nullptr;
	}

	// The external file resolved its own declaration signatures while raising to
	// `INTERFACE_SOLVED`, so the returned node already carries resolved parameter types. The
	// `INTERFACE_SOLVED` status does not run `validate_annotation_declarations()`, which detects
	// same-file duplicate identities, and a single file collapses to one index path so the
	// cross-file duplicate check cannot catch it either. Detect a same-file duplicate here so a
	// usage never binds to an arbitrary one of the conflicting declarations.
	FSParser::AnnotationDeclarationNode *first_match = nullptr;
	for (FSParser::AnnotationDeclarationNode *declaration : external_parser->head->annotation_declarations) {
		if (declaration->qualified_name == p_qualified_name) {
			if (first_match != nullptr) {
				push_error(vformat(R"(Ambiguous annotation "%s": the canonical identity "%s" has multiple declarations.)", p_annotation->name, p_qualified_name), p_annotation);
				r_error_reported = true;
				return nullptr;
			}
			first_match = declaration;
		}
	}
	return first_match;
}

FSParser::AnnotationDeclarationNode *FSAnalyzer::resolve_custom_annotation_declaration(FSParser::AnnotationNode *p_annotation) {
	// Usage names keep the leading "@"; declarations are indexed by their bare short name.
	const String usage_name = String(p_annotation->name);
	const String short_name = usage_name.begins_with("@") ? usage_name.substr(1) : usage_name;

	// A fully qualified usage such as `@cafecito.test.timeout` carries its own canonical identity.
	// It resolves directly against that identity and never depends on the active imports.
	if (short_name.contains_char('.')) {
		return resolve_qualified_annotation_declaration(short_name, p_annotation);
	}

	// 1. The current file's namespace. A declaration in this file is available directly with its
	// resolved signature; the local search also takes precedence over imported namespaces.
	for (FSParser::AnnotationDeclarationNode *declaration : parser->head->annotation_declarations) {
		if (declaration->identifier != nullptr && declaration->identifier->name == StringName(short_name)) {
			resolve_annotation_declaration(declaration);
			return declaration;
		}
	}

	FSLanguage *language = FSLanguage::get_singleton();
	if (language == nullptr) {
		push_error(vformat(R"(Unknown annotation "%s".)", p_annotation->name), p_annotation);
		return nullptr;
	}

	// A sibling file in the same namespace can declare the annotation without an explicit import.
	const String current_namespace = parser->head->namespace_name;
	const String own_identity = current_namespace.is_empty() ? short_name : current_namespace + "." + short_name;
	if (language->is_global_annotation(StringName(own_identity))) {
		// The declaring file enforces its own duplicate-identity error only when analyzed as a
		// head. A usage resolving an externally-duplicated identity would otherwise bind to an
		// arbitrary indexed file, so report the duplicate here instead.
		if (language->is_duplicated_global_annotation(StringName(own_identity))) {
			push_error(vformat(R"(Ambiguous annotation "%s": the canonical identity "%s" is declared in multiple files.)", p_annotation->name, own_identity), p_annotation);
			return nullptr;
		}
		bool error_reported = false;
		FSParser::AnnotationDeclarationNode *declaration = load_external_annotation_declaration(own_identity, p_annotation, error_reported);
		if (declaration != nullptr || error_reported) {
			return declaration;
		}
	}

	// 2. Explicitly imported namespaces. A short name provided by two or more imports is ambiguous.
	Vector<String> matching_namespaces;
	String resolved_identity;
	LocalVector<String> checked_imports;
	for (const String &import : parser->head->imports) {
		if (checked_imports.has(import)) {
			continue;
		}
		checked_imports.push_back(import);
		const String identity = import + "." + short_name;
		if (language->is_global_annotation(StringName(identity))) {
			matching_namespaces.push_back(import);
			resolved_identity = identity;
		}
	}

	if (matching_namespaces.size() > 1) {
		matching_namespaces.sort();
		String namespace_list;
		for (int i = 0; i < matching_namespaces.size(); i++) {
			if (i > 0) {
				namespace_list += i == matching_namespaces.size() - 1 ? " and " : ", ";
			}
			namespace_list += "\"" + matching_namespaces[i] + "\"";
		}
		push_error(vformat(R"(Ambiguous annotation "%s": it is declared in imported namespaces %s.)", p_annotation->name, namespace_list), p_annotation);
		return nullptr;
	}

	if (matching_namespaces.size() == 1) {
		if (language->is_duplicated_global_annotation(StringName(resolved_identity))) {
			push_error(vformat(R"(Ambiguous annotation "%s": the canonical identity "%s" is declared in multiple files.)", p_annotation->name, resolved_identity), p_annotation);
			return nullptr;
		}
		bool error_reported = false;
		FSParser::AnnotationDeclarationNode *declaration = load_external_annotation_declaration(resolved_identity, p_annotation, error_reported);
		if (declaration != nullptr || error_reported) {
			return declaration;
		}
	}

	// 3. No custom declaration is visible. Built-in annotations never reach here.
	push_error(vformat(R"(Unknown annotation "%s". Custom annotations must be declared in the current namespace or an imported namespace.)", p_annotation->name), p_annotation);
	return nullptr;
}

FSParser::AnnotationDeclarationNode *FSAnalyzer::resolve_qualified_annotation_declaration(const String &p_identity, FSParser::AnnotationNode *p_annotation) {
	// `p_identity` is the bare dotted path of the usage, which is also the canonical declaration
	// identity. The leading segments name the declaring namespace and the final segment is the
	// annotation's short name. A qualified usage is unambiguous by construction and is resolved
	// without consulting imports.

	// 1. A declaration in the current file whose canonical identity matches. Searching locally first
	// keeps a same-file declaration authoritative and avoids the index's same-file path guard.
	for (FSParser::AnnotationDeclarationNode *declaration : parser->head->annotation_declarations) {
		if (declaration->qualified_name == p_identity) {
			resolve_annotation_declaration(declaration);
			return declaration;
		}
	}

	FSLanguage *language = FSLanguage::get_singleton();
	if (language == nullptr) {
		push_error(vformat(R"(Unknown annotation "%s".)", p_annotation->name), p_annotation);
		return nullptr;
	}

	// 2. Any indexed declaration with this identity, in any namespace, regardless of imports.
	if (language->is_global_annotation(StringName(p_identity))) {
		if (language->is_duplicated_global_annotation(StringName(p_identity))) {
			push_error(vformat(R"(Ambiguous annotation "%s": the canonical identity "%s" is declared in multiple files.)", p_annotation->name, p_identity), p_annotation);
			return nullptr;
		}
		bool error_reported = false;
		FSParser::AnnotationDeclarationNode *declaration = load_external_annotation_declaration(p_identity, p_annotation, error_reported);
		if (declaration != nullptr || error_reported) {
			return declaration;
		}
	}

	// 3. No declaration carries this identity. Built-in annotations are never dotted.
	push_error(vformat(R"(Unknown annotation "%s". A fully qualified annotation must name an existing declaration.)", p_annotation->name), p_annotation);
	return nullptr;
}

void FSAnalyzer::resolve_custom_annotation(FSParser::AnnotationNode *p_annotation, uint32_t p_target_kind) {
	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;

	FSParser::AnnotationDeclarationNode *declaration = resolve_custom_annotation_declaration(p_annotation);
	if (declaration == nullptr) {
		return;
	}

	p_annotation->resolved_qualified_name = declaration->qualified_name;

	// Validate target. The parser already restricted custom usages to class/method/variable
	// positions; here the declaration's own `targets` set is enforced.
	if (p_target_kind == 0 || (declaration->targets & p_target_kind) == 0) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to %s.)", p_annotation->name, _annotation_target_name(p_target_kind)), p_annotation);
	}

	const int fixed_count = declaration->parameters.size();
	const bool is_variadic = declaration->is_variadic();

	// `0` unbound, `1` bound positionally, `2` bound by name.
	LocalVector<int> binding;
	binding.resize(fixed_count);
	for (int i = 0; i < fixed_count; i++) {
		binding[i] = 0;
	}

	int next_positional = 0;
	bool seen_named = false;
	bool reported_too_many = false;
	bool argument_error = false;

	for (int i = 0; i < p_annotation->arguments.size(); i++) {
		FSParser::ExpressionNode *argument = p_annotation->arguments[i];
		const StringName argument_name = p_annotation->argument_names[i];

		reduce_expression(argument);
		if (!argument->is_constant) {
			push_error(vformat(R"(Argument %d of annotation "%s" is not a constant expression.)", i + 1, p_annotation->name), argument);
			argument_error = true;
			continue;
		}
		Variant value = argument->reduced_value;

		FSParser::ParameterNode *parameter = nullptr;

		if (argument_name == StringName()) {
			if (seen_named) {
				push_error(vformat(R"(Positional argument after named argument in annotation "%s".)", p_annotation->name), argument);
				argument_error = true;
				continue;
			}
			if (next_positional < fixed_count) {
				binding[next_positional] = 1;
				parameter = declaration->parameters[next_positional];
			} else if (is_variadic) {
				parameter = declaration->rest_parameter;
			} else {
				if (!reported_too_many) {
					push_error(vformat(R"(Annotation "%s" takes at most %d argument(s), but %d were given.)", p_annotation->name, fixed_count, p_annotation->arguments.size()), argument);
					reported_too_many = true;
				}
				argument_error = true;
				continue;
			}
			next_positional++;
		} else {
			seen_named = true;
			const int *parameter_index = declaration->parameters_indices.getptr(argument_name);
			if (parameter_index == nullptr) {
				push_error(vformat(R"(Annotation "%s" has no parameter named "%s".)", p_annotation->name, argument_name), argument);
				argument_error = true;
				continue;
			}
			if (binding[*parameter_index] != 0) {
				push_error(vformat(R"(Parameter "%s" of annotation "%s" was specified more than once.)", argument_name, p_annotation->name), argument);
				argument_error = true;
				continue;
			}
			binding[*parameter_index] = 2;
			parameter = declaration->parameters[*parameter_index];
		}

		if (parameter != nullptr) {
			const String context = vformat(R"(argument %d of annotation "%s")", i + 1, p_annotation->name);
			if (!coerce_annotation_argument(parameter->get_datatype(), value, argument, context)) {
				argument_error = true;
				continue;
			}
		}

		p_annotation->resolved_arguments.push_back(value);
	}

	// Required parameters (no default value) must be supplied. Skip this when an argument was
	// already rejected, since the missing binding is a cascade of the earlier diagnostic.
	if (!argument_error) {
		for (int i = 0; i < fixed_count; i++) {
			if (binding[i] == 0 && declaration->parameters[i]->initializer == nullptr) {
				push_error(vformat(R"(Annotation "%s" is missing required argument "%s".)", p_annotation->name, declaration->parameters[i]->identifier->name), p_annotation);
			}
		}
	}
}

void FSAnalyzer::resolve_function_signature(FSParser::FunctionNode *p_function, const FSParser::Node *p_source, bool p_is_lambda) {
	if (p_source == nullptr) {
		p_source = p_function;
	}

	StringName function_name = p_function->identifier != nullptr ? p_function->identifier->name : StringName();
	const bool is_enum_function = p_function->owner_enum != nullptr;

	if (p_function->get_datatype().is_resolving()) {
		push_error(vformat(R"(Could not resolve function "%s": Cyclic reference.)", function_name), p_source);
		return;
	}

	if (p_function->resolved_signature) {
		return;
	}
	p_function->resolved_signature = true;

	FSParser::FunctionNode *previous_function = parser->current_function;
	parser->current_function = p_function;
	const bool previous_resolving_function_signature_type = resolving_function_signature_type;
	resolving_function_signature_type = true;
	bool previous_static_context = static_context;
	if (p_is_lambda) {
		// For lambdas this is determined from the context, the `static` keyword is not allowed.
		p_function->is_static = static_context;
	} else {
		// For normal functions, this is determined in the parser by the `static` keyword.
		static_context = p_function->is_static;
	}

	// Resolve type-parameter bounds as part of the signature (in the function's own scope) so a call
	// site can enforce a generic-method bound even when the bounded parameter appears only in the
	// body, whose resolution may not have run yet.
	for (FSParser::TypeParameterNode *type_parameter : p_function->type_parameters) {
		if (type_parameter != nullptr && type_parameter->bound != nullptr) {
			resolve_datatype(type_parameter->bound);
		}
	}

	MethodInfo method_info;
	method_info.name = function_name;
	if (p_function->is_static) {
		method_info.flags |= MethodFlags::METHOD_FLAG_STATIC;
	}
	if (p_function->is_coroutine) {
		method_info.flags |= MethodFlags::METHOD_FLAG_ASYNC;
	}

	FSParser::DataType prev_datatype = p_function->get_datatype();

	FSParser::DataType resolving_datatype;
	resolving_datatype.kind = FSParser::DataType::RESOLVING;
	p_function->set_datatype(resolving_datatype);

#ifdef TOOLS_ENABLED
	int default_value_count = 0;
#endif // TOOLS_ENABLED

#ifdef DEBUG_ENABLED
	String function_visible_name = function_name;
	if (function_name == StringName()) {
		function_visible_name = p_is_lambda ? "<anonymous lambda>" : "<unknown function>";
	}
#endif // DEBUG_ENABLED

	for (int i = 0; i < p_function->parameters.size(); i++) {
		resolve_parameter(p_function->parameters[i]);
		method_info.arguments.push_back(p_function->parameters[i]->get_datatype().to_property_info(p_function->parameters[i]->identifier->name));
#ifdef DEBUG_ENABLED
		if (p_function->parameters[i]->usages == 0 && !String(p_function->parameters[i]->identifier->name).begins_with("_") && !p_function->is_abstract) {
			parser->push_warning(p_function->parameters[i]->identifier, FSWarning::UNUSED_PARAMETER, function_visible_name, p_function->parameters[i]->identifier->name);
		}
		is_shadowing(p_function->parameters[i]->identifier, "function parameter", true);
#endif // DEBUG_ENABLED

		if (p_function->parameters[i]->initializer) {
#ifdef TOOLS_ENABLED
			default_value_count++;
#endif // TOOLS_ENABLED

			if (p_function->parameters[i]->initializer->is_constant) {
				p_function->default_arg_values.push_back(p_function->parameters[i]->initializer->reduced_value);
			} else {
				p_function->default_arg_values.push_back(Variant()); // Prevent shift.
			}
		}
	}

	if (p_function->is_vararg()) {
		resolve_parameter(p_function->rest_parameter);
		if (p_function->rest_parameter->datatype_specifier != nullptr) {
			FSParser::DataType specified_type = p_function->rest_parameter->get_datatype();
			if (specified_type.kind != FSParser::DataType::BUILTIN || specified_type.builtin_type != Variant::ARRAY) {
				push_error(vformat(R"(The rest parameter type must be "Array", but "%s" is specified.)", specified_type.to_string()), p_function->rest_parameter->datatype_specifier);
			} else if ((specified_type.has_container_element_type(0) && !specified_type.get_container_element_type(0).is_variant())) {
				push_error(R"(Typed arrays are currently not supported for the rest parameter.)", p_function->rest_parameter->datatype_specifier);
			}
		} else {
			FSParser::DataType inferred_type;
			inferred_type.type_source = FSParser::DataType::INFERRED;
			inferred_type.kind = FSParser::DataType::BUILTIN;
			inferred_type.builtin_type = Variant::ARRAY;
			p_function->rest_parameter->set_datatype(inferred_type);
#ifdef DEBUG_ENABLED
			parser->push_warning(p_function->rest_parameter, FSWarning::UNTYPED_DECLARATION, "Parameter", p_function->rest_parameter->identifier->name);
#endif
		}
#ifdef DEBUG_ENABLED
		if (p_function->rest_parameter->usages == 0 && !String(p_function->rest_parameter->identifier->name).begins_with("_") && !p_function->is_abstract) {
			parser->push_warning(p_function->rest_parameter->identifier, FSWarning::UNUSED_PARAMETER, function_visible_name, p_function->rest_parameter->identifier->name);
		}
		is_shadowing(p_function->rest_parameter->identifier, "function parameter", true);
#endif // DEBUG_ENABLED
	}

	if (!p_is_lambda && !is_enum_function && function_name == FSLanguage::get_singleton()->strings._init) {
		// Constructor.
		FSParser::DataType return_type = parser->current_class->get_datatype();
		return_type.is_meta_type = false;
		p_function->set_datatype(return_type);
		if (p_function->return_type) {
			FSParser::DataType declared_return = resolve_datatype(p_function->return_type);
			if (declared_return.kind != FSParser::DataType::BUILTIN || declared_return.builtin_type != Variant::NIL) {
				push_error("Constructor cannot have an explicit return type.", p_function->return_type);
			}
		}
	} else if (!p_is_lambda && !is_enum_function && function_name == FSLanguage::get_singleton()->strings._static_init) {
		// Static constructor.
		FSParser::DataType return_type;
		return_type.kind = FSParser::DataType::BUILTIN;
		return_type.builtin_type = Variant::NIL;
		p_function->set_datatype(return_type);
		if (p_function->return_type) {
			FSParser::DataType declared_return = resolve_datatype(p_function->return_type);
			if (declared_return.kind != FSParser::DataType::BUILTIN || declared_return.builtin_type != Variant::NIL) {
				push_error("Static constructor cannot have an explicit return type.", p_function->return_type);
			}
		}
	} else {
		if (p_function->return_type != nullptr) {
			p_function->set_datatype(type_from_metatype(resolve_datatype(p_function->return_type)));
		} else {
			// In case the function is not typed, we can safely assume it's a Variant, so it's okay to mark as "inferred" here.
			// It's not "undetected" to not mix up with unknown functions.
			FSParser::DataType return_type;
			return_type.type_source = FSParser::DataType::INFERRED;
			return_type.kind = FSParser::DataType::VARIANT;
			p_function->set_datatype(return_type);
		}

		// Resolve the matching parent method once and share the result between the final-override
		// check (enforced in all builds) and the signature-compatibility check (editor-only below).
		// A second resolution would re-run member resolution and duplicate any errors it emits.
		// Not for the constructor, which can vary in signature.
		FSParser::DataType base_type = parser->current_class->base_type;
		base_type.is_meta_type = false;
		FSParser::DataType parent_return_type;
		List<FSParser::DataType> parameters_types;
		int default_par_count = 0;
		BitField<MethodFlags> method_flags = {};
		StringName native_base;
		FSParser::FunctionNode *parent_function = nullptr;
		FSParser::ClassNode *parent_function_class = nullptr;
		const FSParser::DataType override_self_type = _self_type_for_class(parser->current_class);
		const bool has_parent_signature = !p_is_lambda && !is_enum_function && get_function_signature(p_function, false, base_type, function_name, parent_return_type, parameters_types, default_par_count, method_flags, &native_base, nullptr, &parent_function, &parent_function_class, &override_self_type);

		// get_function_signature reports an async parent's return as Coroutine[T], but a function's own
		// declared return type is the raw T. Async-ness is checked separately via METHOD_FLAG_ASYNC, so
		// compare (and render) the underlying result type to keep override covariance honest.
		if (parent_return_type.is_coroutine && parent_return_type.has_container_element_type(0)) {
			parent_return_type = parent_return_type.get_container_element_type(0);
		}

		// A final method cannot be overridden in a subclass. Enforced in all builds (not just
		// editor/tools), mirroring final-class enforcement, since it is a language rule rather
		// than an editor diagnostic, and reported independently of signature compatibility so the
		// more fundamental violation surfaces first.
		if (has_parent_signature && parent_function != nullptr && parent_function->is_final) {
			push_error(vformat(R"*(Cannot override final function "%s()" declared in "%s".)*", function_name, _class_or_trait_name(parent_function_class)), p_function);
		}

#ifdef TOOLS_ENABLED
		// Check if the function signature matches the parent. If not it's an error since it breaks polymorphism.
		if (has_parent_signature) {
			bool valid = p_function->is_static == method_flags.has_flag(METHOD_FLAG_STATIC);
			const bool parent_is_coroutine = method_flags.has_flag(METHOD_FLAG_ASYNC);
			const bool current_is_coroutine = p_function->is_coroutine;
			bool valid_coroutine_override = parent_is_coroutine == current_is_coroutine;
			if (!valid_coroutine_override && !parent_is_coroutine && current_is_coroutine &&
					FSScriptExtensibleNativeHooks::allows_async_override_of_sync_hook(native_base, function_name)) {
				valid_coroutine_override = true;
			}
			valid = valid && valid_coroutine_override;

			if (p_function->return_type != nullptr) {
				// Check return type covariance.
				FSParser::DataType return_type = p_function->get_datatype();
				if (return_type.is_variant()) {
					// `is_type_compatible()` returns `true` if one of the types is `Variant`.
					// Don't allow an explicitly specified `Variant` if the parent return type is narrower.
					valid = valid && parent_return_type.is_variant();
				} else if (return_type.kind == FSParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL) {
					// `is_type_compatible()` returns `true` if target is an `Object` and source is `null`.
					// Don't allow `void` if the parent return type is a hard non-`void` type.
					if (parent_return_type.is_hard_type() && !(parent_return_type.kind == FSParser::DataType::BUILTIN && parent_return_type.builtin_type == Variant::NIL)) {
						valid = false;
					}
				} else if (parent_return_type.is_set() && return_type.is_set()) {
					// Skip while a type is still resolving (re-entrant member resolution);
					// `is_type_compatible()` treats an unset type as compatible anyway.
					valid = valid && is_type_compatible(parent_return_type, return_type);
				}
			}

			int parent_min_argc = parameters_types.size() - default_par_count;
			int parent_max_argc = (method_flags & METHOD_FLAG_VARARG) ? INT_MAX : parameters_types.size();
			int current_min_argc = p_function->parameters.size() - default_value_count;
			int current_max_argc = p_function->is_vararg() ? INT_MAX : p_function->parameters.size();

			// `[current_min_argc..current_max_argc]` must include `[parent_min_argc..parent_max_argc]`.
			valid = valid && current_min_argc <= parent_min_argc && parent_max_argc <= current_max_argc;

			if (valid) {
				int i = 0;
				for (const FSParser::DataType &parent_par_type : parameters_types) {
					if (i >= p_function->parameters.size()) {
						break;
					}
					const FSParser::DataType &current_par_type = p_function->parameters[i]->datatype;
					i++;
					// Check parameter type contravariance.
					if (parent_par_type.is_variant() && parent_par_type.is_hard_type()) {
						// `is_type_compatible()` returns `true` if one of the types is `Variant`.
						// Don't allow narrowing a hard `Variant`.
						valid = valid && current_par_type.is_variant();
					} else if (current_par_type.is_set() && parent_par_type.is_set()) {
						// Skip while a type is still resolving (re-entrant member resolution);
						// `is_type_compatible()` treats an unset type as compatible anyway.
						valid = valid && is_type_compatible(current_par_type, parent_par_type);
					}
				}
			}

			if (!valid_coroutine_override) {
				if (parent_is_coroutine) {
					push_error(vformat(R"*(The function "%s()" must be async because it overrides an async parent function.)*", function_name), p_function);
				} else {
					push_error(vformat(R"*(The function "%s()" cannot be async because it overrides a synchronous parent function.)*", function_name), p_function);
				}
			} else if (!valid) {
				// Compute parent signature as a string to show in the error message.
				String parent_signature = String(function_name) + "(";
				int j = 0;
				for (const FSParser::DataType &par_type : parameters_types) {
					if (j > 0) {
						parent_signature += ", ";
					}
					String parameter = par_type.to_string();
					if (parameter == "null") {
						parameter = "Variant";
					}
					parent_signature += parameter;
					if (j >= parameters_types.size() - default_par_count) {
						parent_signature += " = <default>";
					}

					j++;
				}
				if (method_flags & METHOD_FLAG_VARARG) {
					if (!parameters_types.is_empty()) {
						parent_signature += ", ";
					}
					parent_signature += "...";
				}
				parent_signature += ") -> ";

				const String return_type = parent_return_type.to_string_strict();
				if (return_type == "null") {
					parent_signature += "void";
				} else {
					parent_signature += return_type;
				}

				push_error(vformat(R"(The function signature doesn't match the parent. Parent signature is "%s".)", parent_signature), p_function);
			}
#ifdef DEBUG_ENABLED
			if (native_base != StringName() && !FSScriptExtensibleNativeHooks::is_allowed_override(native_base, function_name) &&
					!(parser->current_class->is_trait && p_function->is_abstract)) {
				parser->push_warning(p_function, FSWarning::NATIVE_METHOD_OVERRIDE, function_name, native_base);
			}
#endif // DEBUG_ENABLED
		}
#endif // TOOLS_ENABLED
	}

#ifdef DEBUG_ENABLED
	if (p_function->return_type == nullptr) {
		parser->push_warning(p_function, FSWarning::UNTYPED_DECLARATION, "Function", function_visible_name);
	}
#endif // DEBUG_ENABLED

	method_info.default_arguments.append_array(p_function->default_arg_values);
	method_info.return_val = p_function->get_datatype().to_property_info("");
	p_function->info = method_info;

	if (p_function->get_datatype().is_resolving()) {
		p_function->set_datatype(prev_datatype);
	}

	parser->current_function = previous_function;
	resolving_function_signature_type = previous_resolving_function_signature_type;
	static_context = previous_static_context;
}

void FSAnalyzer::resolve_function_body(FSParser::FunctionNode *p_function, bool p_is_lambda) {
	require_completed_analyzer_phase(AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE, AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL);

	if (p_function->resolved_body) {
		return;
	}
	p_function->resolved_body = true;

	if (p_function->body->statements.is_empty()) {
		// Non-abstract functions must have a body.
		if (p_function->source_lambda != nullptr) {
			push_error(R"(A lambda function must have a ":" followed by a body.)", p_function);
		} else if (!p_function->is_abstract) {
			push_error(R"(A function must either have a ":" followed by a body, or be marked as "abstract".)", p_function);
		}
		return;
	} else {
		// Abstract functions must not have a body.
		if (p_function->is_abstract) {
			push_error(R"(An abstract function cannot have a body.)", p_function->body);
			return;
		}
	}

	FSParser::FunctionNode *previous_function = parser->current_function;
	parser->current_function = p_function;

	bool previous_static_context = static_context;
	static_context = p_function->is_static;

	{
		FlowFinalityContext::FlowNarrowingScope flow_scope(flow_finality, !p_is_lambda);
		resolve_suite(p_function->body);
	}

	const SuiteExitState body_exit = get_suite_exit_state(p_function->body);
	warn_unreachable_after_noreturn(p_function->body);

	if (p_function->is_noreturn) {
		if (body_exit.has_return) {
			push_error(R"(A "@noreturn" function cannot return.)", p_function);
		} else if (!body_exit.always_terminates) {
			push_error(R"(A "@noreturn" function cannot complete normally.)", p_function);
		}
	}

	if (!p_function->get_datatype().is_hard_type() && p_function->body->get_datatype().is_set()) {
		// Use the suite inferred type if return isn't explicitly set.
		p_function->set_datatype(p_function->body->get_datatype());
	} else if (p_function->get_datatype().is_hard_type() && (p_function->get_datatype().kind != FSParser::DataType::BUILTIN || p_function->get_datatype().builtin_type != Variant::NIL)) {
		if (!body_exit.always_terminates && (p_is_lambda || p_function->identifier->name != FSLanguage::get_singleton()->strings._init)) {
			push_error(R"(Not all code paths return a value.)", p_function);
		}
	}

	parser->current_function = previous_function;
	static_context = previous_static_context;
}

FSAnalyzer::SuiteExitState FSAnalyzer::get_suite_exit_state(const FSParser::SuiteNode *p_suite) const {
	SuiteExitState result;
	if (p_suite == nullptr) {
		return result;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		const SuiteExitState statement_exit = get_statement_exit_state(statement);
		result.has_return = result.has_return || statement_exit.has_return;
		result.has_noreturn = result.has_noreturn || statement_exit.has_noreturn;

		if (statement_exit.always_terminates) {
			result.always_terminates = true;
			return result;
		}
	}

	return result;
}

FSAnalyzer::SuiteExitState FSAnalyzer::get_statement_exit_state(const FSParser::Node *p_statement) const {
	SuiteExitState result;
	if (p_statement == nullptr) {
		return result;
	}

	switch (p_statement->type) {
		case FSParser::Node::RETURN:
			result.always_terminates = true;
			result.has_return = true;
			break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_statement);
			if (call->is_noreturn) {
				result.always_terminates = true;
				result.has_noreturn = true;
			}
		} break;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			const SuiteExitState true_exit = get_suite_exit_state(if_node->true_block);
			const SuiteExitState false_exit = get_suite_exit_state(if_node->false_block);

			result.has_return = true_exit.has_return || false_exit.has_return;
			result.has_noreturn = true_exit.has_noreturn || false_exit.has_noreturn;
			result.always_terminates = if_node->false_block != nullptr && true_exit.always_terminates && false_exit.always_terminates;
		} break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			bool all_branches_terminate = !match_node->branches.is_empty();
			bool has_wildcard = false;
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				const SuiteExitState branch_exit = get_suite_exit_state(branch->block);
				result.has_return = result.has_return || branch_exit.has_return;
				result.has_noreturn = result.has_noreturn || branch_exit.has_noreturn;
				all_branches_terminate = all_branches_terminate && branch_exit.always_terminates;
				has_wildcard = has_wildcard || branch->has_wildcard;
			}
			result.always_terminates = has_wildcard && all_branches_terminate;
		} break;
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_statement);
			const SuiteExitState loop_exit = get_suite_exit_state(while_node->loop);
			result.has_return = loop_exit.has_return;
			result.has_noreturn = loop_exit.has_noreturn;
			result.always_terminates = while_node->condition != nullptr && while_node->condition->is_constant &&
					while_node->condition->reduced_value.booleanize() && !suite_has_reachable_break(while_node->loop);
		} break;
		case FSParser::Node::SUITE:
			result = get_suite_exit_state(static_cast<const FSParser::SuiteNode *>(p_statement));
			break;
		default:
			break;
	}

	return result;
}

bool FSAnalyzer::suite_has_reachable_break(const FSParser::SuiteNode *p_suite) const {
	if (p_suite == nullptr) {
		return false;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement_has_reachable_break(statement)) {
			return true;
		}
		if (get_statement_exit_state(statement).always_terminates) {
			return false;
		}
	}
	return false;
}

bool FSAnalyzer::statement_has_reachable_break(const FSParser::Node *p_statement) const {
	if (p_statement == nullptr) {
		return false;
	}

	switch (p_statement->type) {
		case FSParser::Node::BREAK:
			return true;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			return suite_has_reachable_break(if_node->true_block) || suite_has_reachable_break(if_node->false_block);
		}
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				if (suite_has_reachable_break(branch->block)) {
					return true;
				}
			}
			return false;
		}
		case FSParser::Node::SUITE:
			return suite_has_reachable_break(static_cast<const FSParser::SuiteNode *>(p_statement));
		default:
			return false;
	}
}

void FSAnalyzer::warn_unreachable_after_noreturn(const FSParser::SuiteNode *p_suite) {
#ifdef DEBUG_ENABLED
	if (p_suite == nullptr) {
		return;
	}

	for (int i = 0; i < p_suite->statements.size(); i++) {
		const FSParser::Node *statement = p_suite->statements[i];
		warn_unreachable_after_noreturn_in_statement(statement);

		const SuiteExitState statement_exit = get_statement_exit_state(statement);
		if (statement_exit.always_terminates) {
			if (statement_exit.has_noreturn && i + 1 < p_suite->statements.size()) {
				const StringName function_name = parser->current_function && parser->current_function->identifier ? parser->current_function->identifier->name : StringName("<anonymous lambda>");
				parser->push_warning(p_suite->statements[i + 1], FSWarning::UNREACHABLE_CODE, function_name);
			}
			return;
		}
	}
#else
	(void)p_suite;
#endif // DEBUG_ENABLED
}

void FSAnalyzer::warn_unreachable_after_noreturn_in_statement(const FSParser::Node *p_statement) {
#ifdef DEBUG_ENABLED
	if (p_statement == nullptr) {
		return;
	}

	switch (p_statement->type) {
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			warn_unreachable_after_noreturn(if_node->true_block);
			warn_unreachable_after_noreturn(if_node->false_block);
		} break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				warn_unreachable_after_noreturn(branch->block);
			}
		} break;
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_statement);
			warn_unreachable_after_noreturn(while_node->loop);
		} break;
		case FSParser::Node::FOR: {
			const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_statement);
			warn_unreachable_after_noreturn(for_node->loop);
		} break;
		case FSParser::Node::SUITE:
			warn_unreachable_after_noreturn(static_cast<const FSParser::SuiteNode *>(p_statement));
			break;
		default:
			break;
	}
#else
	(void)p_statement;
#endif // DEBUG_ENABLED
}

void FSAnalyzer::decide_suite_type(FSParser::Node *p_suite, FSParser::Node *p_statement) {
	if (p_statement == nullptr) {
		return;
	}
	switch (p_statement->type) {
		case FSParser::Node::IF:
		case FSParser::Node::FOR:
		case FSParser::Node::MATCH:
		case FSParser::Node::PATTERN:
		case FSParser::Node::RETURN:
		case FSParser::Node::WHILE:
			// Use return or nested suite type as this suite type.
			if (p_suite->get_datatype().is_set() && (p_suite->get_datatype() != p_statement->get_datatype())) {
				// Mixed types.
				// TODO: This could use the common supertype instead.
				p_suite->datatype.kind = FSParser::DataType::VARIANT;
				p_suite->datatype.type_source = FSParser::DataType::UNDETECTED;
			} else {
				p_suite->set_datatype(p_statement->get_datatype());
				p_suite->datatype.type_source = FSParser::DataType::INFERRED;
			}
			break;
		default:
			break;
	}
}

void FSAnalyzer::resolve_suite(FSParser::SuiteNode *p_suite, bool p_is_root) {
	for (int i = 0; i < p_suite->statements.size(); i++) {
		FSParser::Node *stmt = p_suite->statements[i];
		// Apply annotations.
		for (FSParser::AnnotationNode *&E : stmt->annotations) {
			resolve_annotation(E);
			E->apply(parser, stmt, nullptr); // TODO: Provide `p_class`.
		}

		resolve_node(stmt, p_is_root);
		resolve_pending_lambda_bodies();
		decide_suite_type(p_suite, stmt);
	}
}

void FSAnalyzer::resolve_assignable(FSParser::AssignableNode *p_assignable, const char *p_kind) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::VARIANT;

	bool is_constant = p_assignable->type == FSParser::Node::CONSTANT;

#ifdef DEBUG_ENABLED
	if (p_assignable->identifier != nullptr && p_assignable->identifier->suite != nullptr && p_assignable->identifier->suite->parent_block != nullptr) {
		if (p_assignable->identifier->suite->parent_block->has_local(p_assignable->identifier->name)) {
			const FSParser::SuiteNode::Local &local = p_assignable->identifier->suite->parent_block->get_local(p_assignable->identifier->name);
			parser->push_warning(p_assignable->identifier, FSWarning::CONFUSABLE_LOCAL_DECLARATION, local.get_name(), p_assignable->identifier->name);
		}
	}
#endif // DEBUG_ENABLED

	FSParser::DataType specified_type;
	bool has_specified_type = p_assignable->datatype_specifier != nullptr;
	if (has_specified_type) {
		specified_type = type_from_metatype(resolve_datatype(p_assignable->datatype_specifier));
		type = specified_type;
	}

	if (p_assignable->initializer != nullptr) {
		reduce_expression(p_assignable->initializer);

		if (p_assignable->initializer->type == FSParser::Node::ARRAY) {
			FSParser::ArrayNode *array = static_cast<FSParser::ArrayNode *>(p_assignable->initializer);
			if (has_specified_type && specified_type.has_container_element_type(0)) {
				update_array_literal_element_type(array, specified_type.get_container_element_type(0));
			}
		} else if (p_assignable->initializer->type == FSParser::Node::DICTIONARY) {
			FSParser::DictionaryNode *dictionary = static_cast<FSParser::DictionaryNode *>(p_assignable->initializer);
			if (has_specified_type && specified_type.has_container_element_types()) {
				update_dictionary_literal_element_type(dictionary, specified_type.get_container_element_type_or_variant(0), specified_type.get_container_element_type_or_variant(1));
			}
		}

		if (is_constant && !p_assignable->initializer->is_constant) {
			bool is_initializer_value_reduced = false;
			Variant initializer_value = make_expression_reduced_value(p_assignable->initializer, is_initializer_value_reduced);
			if (is_initializer_value_reduced) {
				p_assignable->initializer->is_constant = true;
				p_assignable->initializer->reduced_value = initializer_value;
			} else {
				push_error(vformat(R"(Assigned value for %s "%s" isn't a constant expression.)", p_kind, p_assignable->identifier->name), p_assignable->initializer);
			}
		}

		if (has_specified_type && p_assignable->initializer->is_constant) {
			update_const_expression_builtin_type(p_assignable->initializer, specified_type, "assign");
		}
		FSParser::DataType initializer_type = p_assignable->initializer->get_datatype();

		if (p_assignable->infer_datatype) {
			if (!initializer_type.is_set() || initializer_type.has_no_type() || !initializer_type.is_hard_type()) {
				push_error(vformat(R"(Cannot infer the type of "%s" %s because the value doesn't have a set type.)", p_assignable->identifier->name, p_kind), p_assignable->initializer);
			} else if (initializer_type.kind == FSParser::DataType::BUILTIN && initializer_type.builtin_type == Variant::NIL && !is_constant) {
				push_error(vformat(R"(Cannot infer the type of "%s" %s because the value is "null".)", p_assignable->identifier->name, p_kind), p_assignable->initializer);
			}
#ifdef DEBUG_ENABLED
			if (initializer_type.is_hard_type() && initializer_type.is_variant()) {
				parser->push_warning(p_assignable, FSWarning::INFERENCE_ON_VARIANT, p_kind);
			}
#endif // DEBUG_ENABLED
		} else {
			if (!initializer_type.is_set()) {
				push_error(vformat(R"(Could not resolve type for %s "%s".)", p_kind, p_assignable->identifier->name), p_assignable->initializer);
			}
		}

		if (!has_specified_type) {
			type = initializer_type;

			if (!type.is_set() || (type.is_hard_type() && type.kind == FSParser::DataType::BUILTIN && type.builtin_type == Variant::NIL && !is_constant)) {
				type.kind = FSParser::DataType::VARIANT;
			}

			if (p_assignable->infer_datatype || is_constant) {
				type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
			} else {
				type.type_source = FSParser::DataType::INFERRED;
			}
		} else if (!specified_type.is_variant()) {
			if (_datatype_contains_self_type_parameter(specified_type)) {
				if (!initializer_type.is_hard_type() || !_datatype_matches_self_return_contract(specified_type, initializer_type)) {
					push_error(vformat(R"(Cannot assign a value of type %s to %s "%s" with specified type %s.)",
									   initializer_type.to_string(),
									   p_kind,
									   p_assignable->identifier->name,
									   specified_type.to_string()),
							p_assignable->initializer);
				}
			} else if (initializer_type.is_variant() || !initializer_type.is_hard_type()) {
				if (initializer_type.is_variant() && strict_dynamic_checks) {
					push_error(vformat(R"(Cannot assign Variant value to %s "%s" in strict dynamic mode; expected "%s".)",
									   p_kind,
									   p_assignable->identifier->name,
									   specified_type.to_string()),
							p_assignable->initializer);
				} else {
					mark_node_unsafe(p_assignable->initializer);
					p_assignable->use_conversion_assign = true;
				}
				if (!initializer_type.is_variant() && !is_type_compatible(specified_type, initializer_type, true, p_assignable->initializer)) {
					downgrade_node_type_source(p_assignable->initializer);
				}
			} else if (!is_type_compatible(specified_type, initializer_type, true, p_assignable->initializer)) {
				const bool nullable_mismatch = strict_null_checks && initializer_type.is_nullable && !specified_type.is_nullable && !specified_type.is_variant();
				String type_handle_error;
				if (!nullable_mismatch) {
					type_handle_error = _make_type_handle_assignment_error(
							specified_type,
							initializer_type,
							p_assignable->initializer,
							p_kind,
							p_assignable->identifier->name,
							true);
				}
				if (!type_handle_error.is_empty()) {
					push_error(type_handle_error, p_assignable->initializer);
				} else if (!nullable_mismatch && !is_constant && FSTypeCompatibility::allows_runtime_narrowing(specified_type, initializer_type)) {
					mark_node_unsafe(p_assignable->initializer);
					p_assignable->use_conversion_assign = true;
				} else {
					if (nullable_mismatch) {
						push_error(vformat(R"(Cannot assign nullable value of type "%s" to %s "%s"; expected non-nullable "%s".)",
										   initializer_type.to_string(),
										   p_kind,
										   p_assignable->identifier->name,
										   specified_type.to_string()),
								p_assignable->initializer);
					} else {
						push_error(vformat(R"(Cannot assign a value of type %s to %s "%s" with specified type %s.)",
										   initializer_type.to_string(),
										   p_kind,
										   p_assignable->identifier->name,
										   specified_type.to_string()),
								p_assignable->initializer);
					}
				}
			} else if ((specified_type.has_container_element_type(0) && !initializer_type.has_container_element_type(0)) || (specified_type.has_container_element_type(1) && !initializer_type.has_container_element_type(1))) {
				mark_node_unsafe(p_assignable->initializer);
#ifdef DEBUG_ENABLED
			} else if (specified_type.builtin_type == Variant::INT && initializer_type.builtin_type == Variant::FLOAT) {
				parser->push_warning(p_assignable->initializer, FSWarning::NARROWING_CONVERSION);
#endif // DEBUG_ENABLED
			}
		}

		mark_coroutine_handle_capture(p_assignable->initializer, type);
	}

#ifdef DEBUG_ENABLED
	const bool is_parameter = p_assignable->type == FSParser::Node::PARAMETER;
	if (!has_specified_type) {
		const String declaration_type = is_constant ? "Constant" : (is_parameter ? "Parameter" : "Variable");
		if (p_assignable->infer_datatype || is_constant) {
			// Do not produce the `INFERRED_DECLARATION` warning on type import because there is no way to specify the true type.
			// And removing the metatype makes it impossible to use the constant as a type hint (especially for enums).
			const bool is_type_import = is_constant && p_assignable->initializer != nullptr && p_assignable->initializer->datatype.is_meta_type;
			if (!is_type_import) {
				parser->push_warning(p_assignable, FSWarning::INFERRED_DECLARATION, declaration_type, p_assignable->identifier->name);
			}
		} else {
			parser->push_warning(p_assignable, FSWarning::UNTYPED_DECLARATION, declaration_type, p_assignable->identifier->name);
		}
	} else if (!is_parameter && specified_type.kind == FSParser::DataType::ENUM && p_assignable->initializer == nullptr) {
		// Warn about enum variables without default value. Unless the enum defines the "0" value, then it's fine.
		// A tagged union never has a usable implicit default: its values are case Arrays, so the case
		// with tag 0 is not what an uninitialized variable holds.
		bool has_zero_value = false;
		if (!specified_type.is_tagged_union) {
			for (const KeyValue<StringName, int64_t> &kv : specified_type.enum_values) {
				if (kv.value == 0) {
					has_zero_value = true;
					break;
				}
			}
		}
		if (!has_zero_value) {
			parser->push_warning(p_assignable, FSWarning::ENUM_VARIABLE_WITHOUT_DEFAULT, p_assignable->identifier->name);
		}
	}
#endif // DEBUG_ENABLED

	type.is_constant = is_constant;
	type.is_read_only = false;
	p_assignable->set_datatype(type);
}

void FSAnalyzer::resolve_variable(FSParser::VariableNode *p_variable, bool p_is_local) {
	static constexpr const char *kind = "variable";
	resolve_assignable(p_variable, kind);

#ifdef DEBUG_ENABLED
	if (p_is_local) {
		if (p_variable->usages == 0 && !String(p_variable->identifier->name).begins_with("_")) {
			parser->push_warning(p_variable, FSWarning::UNUSED_VARIABLE, p_variable->identifier->name);
		}
	}
	is_shadowing(p_variable->identifier, kind, p_is_local);
#endif // DEBUG_ENABLED
}

// Types the bindings of a destructuring declaration. The initializer must be a statically known
// tuple of exactly the declared arity: a Variant or any other shape has no element types to hand
// out, so it is rejected rather than silently degrading every binding to Variant.
void FSAnalyzer::resolve_variable_destructure(FSParser::VariableDestructureNode *p_destructure) {
	FSParser::DataType initializer_type;
	if (p_destructure->initializer != nullptr) {
		reduce_expression(p_destructure->initializer);
		initializer_type = p_destructure->initializer->get_datatype();
	}

	bool shape_is_known = false;
	if (p_destructure->initializer == nullptr) {
		// The parser already reported the missing initializer.
	} else if (!initializer_type.is_set() || !initializer_type.is_hard_type() || initializer_type.kind != FSParser::DataType::TUPLE) {
		push_error(vformat(R"(Cannot destructure a value of type "%s"; only a tuple with a statically known shape can be destructured.)",
						   initializer_type.to_string()),
				p_destructure->initializer);
	} else if (initializer_type.is_meta_type) {
		push_error(vformat(R"(Cannot destructure the tuple type "%s"; construct a value first.)", initializer_type.to_string()),
				p_destructure->initializer);
	} else if (strict_null_checks && initializer_type.is_nullable) {
		// Destructuring reads the elements, so it dereferences the value: a nullable tuple must be
		// narrowed to non-null first, exactly like the other strict-null boundaries.
		push_error(vformat(R"(Cannot destructure the nullable value of type "%s"; check for null first.)", initializer_type.to_string()),
				p_destructure->initializer);
	} else if (initializer_type.container_element_types.size() != p_destructure->bindings.size()) {
		push_error(vformat(R"(Cannot destructure the tuple "%s" into %d bindings; it has %d elements.)",
						   initializer_type.to_string(), p_destructure->bindings.size(), initializer_type.container_element_types.size()),
				p_destructure->initializer);
	} else {
		shape_is_known = true;
	}

	for (int i = 0; i < p_destructure->bindings.size(); i++) {
		FSParser::VariableNode *binding = p_destructure->bindings[i];
		if (binding == nullptr) {
			continue; // A `_` slot binds nothing.
		}

		FSParser::DataType binding_type;
		if (shape_is_known) {
			binding_type = initializer_type.get_container_element_type_or_variant(i);
			binding_type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
		} else {
			// Keep going with a Variant binding so a broken initializer reports once instead of
			// cascading through every later use of the names it declares.
			binding_type.kind = FSParser::DataType::VARIANT;
			binding_type.type_source = FSParser::DataType::UNDETECTED;
		}
		// A `const` binding is immutable, not compile-time constant: its value comes from a runtime
		// tuple, so it must not be treated as a foldable constant.
		binding_type.is_constant = false;
		binding_type.is_read_only = false;
		binding->set_datatype(binding_type);

#ifdef DEBUG_ENABLED
		if (binding->usages == 0 && !String(binding->identifier->name).begins_with("_")) {
			parser->push_warning(binding, FSWarning::UNUSED_VARIABLE, binding->identifier->name);
		}
		is_shadowing(binding->identifier, "variable", true);
#endif // DEBUG_ENABLED
	}
}

void FSAnalyzer::resolve_constant(FSParser::ConstantNode *p_constant, bool p_is_local) {
	static constexpr const char *kind = "constant";
	resolve_assignable(p_constant, kind);

#ifdef DEBUG_ENABLED
	if (p_is_local) {
		if (p_constant->usages == 0 && !String(p_constant->identifier->name).begins_with("_")) {
			parser->push_warning(p_constant, FSWarning::UNUSED_LOCAL_CONSTANT, p_constant->identifier->name);
		}
	}
	is_shadowing(p_constant->identifier, kind, p_is_local);
#endif // DEBUG_ENABLED
}

void FSAnalyzer::resolve_parameter(FSParser::ParameterNode *p_parameter) {
	static constexpr const char *kind = "parameter";
	for (FSParser::AnnotationNode *&E : p_parameter->annotations) {
		resolve_annotation(E, FSParser::AnnotationDeclarationNode::TARGET_PARAMETER);
		E->apply(parser, p_parameter, parser->current_class);
	}
	resolve_assignable(p_parameter, kind);
}

void FSAnalyzer::resolve_if(FSParser::IfNode *p_if) {
	flow_finality.reduce_condition_expression(p_if->condition);

	HashMap<const FSParser::Node *, FSParser::DataType> previous_flow_narrowed_types = flow_finality.get_flow_narrowed_types();
	flow_finality.apply_flow_narrowing_from_condition(p_if->condition, true);
	resolve_suite(p_if->true_block);
	flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;
	p_if->set_datatype(p_if->true_block->get_datatype());

	if (p_if->false_block != nullptr) {
		previous_flow_narrowed_types = flow_finality.get_flow_narrowed_types();
		flow_finality.apply_flow_narrowing_from_condition(p_if->condition, false);
		if (FSParser::IfNode *elif = p_if->get_elif()) {
			resolve_if(elif);
			decide_suite_type(p_if, elif);
		} else {
			resolve_suite(p_if->false_block);
			decide_suite_type(p_if, p_if->false_block);
		}
		flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;
	}
}

void FSAnalyzer::resolve_for(FSParser::ForNode *p_for) {
	FSParser::DataType variable_type;
	FSParser::DataType list_type;

	if (p_for->list) {
		resolve_node(p_for->list, false);

		bool is_range = false;
		if (p_for->list->type == FSParser::Node::CALL) {
			FSParser::CallNode *call = static_cast<FSParser::CallNode *>(p_for->list);
			if (call->get_callee_type() == FSParser::Node::IDENTIFIER) {
				if (static_cast<FSParser::IdentifierNode *>(call->callee)->name == "range") {
					if (call->arguments.is_empty()) {
						push_error(R"*(Invalid call for "range()" function. Expected at least 1 argument, none given.)*", call);
					} else if (call->arguments.size() > 3) {
						push_error(vformat(R"*(Invalid call for "range()" function. Expected at most 3 arguments, %d given.)*", call->arguments.size()), call);
					}
					is_range = true;
					variable_type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
					variable_type.kind = FSParser::DataType::BUILTIN;
					variable_type.builtin_type = Variant::INT;
				}
			}
		}

		list_type = p_for->list->get_datatype();

		if (!list_type.is_hard_type()) {
			mark_node_unsafe(p_for->list);
		}

		if (is_range) {
			// Already solved.
		} else if (list_type.is_variant()) {
			variable_type.kind = FSParser::DataType::VARIANT;
			mark_node_unsafe(p_for->list);
		} else if (list_type.has_container_element_type(0)) {
			variable_type = list_type.get_container_element_type(0);
			variable_type.type_source = list_type.type_source;
		} else if (list_type.is_typed_container_type()) {
			variable_type = list_type.get_typed_container_type();
			variable_type.type_source = list_type.type_source;
		} else if (list_type.builtin_type == Variant::INT || list_type.builtin_type == Variant::FLOAT || list_type.builtin_type == Variant::STRING) {
			variable_type.type_source = list_type.type_source;
			variable_type.kind = FSParser::DataType::BUILTIN;
			variable_type.builtin_type = list_type.builtin_type;
		} else if (list_type.builtin_type == Variant::VECTOR2I || list_type.builtin_type == Variant::VECTOR3I) {
			variable_type.type_source = list_type.type_source;
			variable_type.kind = FSParser::DataType::BUILTIN;
			variable_type.builtin_type = Variant::INT;
		} else if (list_type.builtin_type == Variant::VECTOR2 || list_type.builtin_type == Variant::VECTOR3) {
			variable_type.type_source = list_type.type_source;
			variable_type.kind = FSParser::DataType::BUILTIN;
			variable_type.builtin_type = Variant::FLOAT;
		} else if (list_type.builtin_type == Variant::OBJECT) {
			FSParser::DataType return_type;
			List<FSParser::DataType> par_types;
			int default_arg_count = 0;
			BitField<MethodFlags> method_flags = {};
			if (get_function_signature(p_for->list, false, list_type, CoreStringName(_iter_get), return_type, par_types, default_arg_count, method_flags)) {
				variable_type = return_type;
				variable_type.type_source = list_type.type_source;
			} else if (!list_type.is_hard_type()) {
				variable_type.kind = FSParser::DataType::VARIANT;
			} else {
				push_error(vformat(R"(Unable to iterate on object of type "%s".)", list_type.to_string()), p_for->list);
			}
		} else if (list_type.builtin_type == Variant::ARRAY || list_type.builtin_type == Variant::DICTIONARY || !list_type.is_hard_type()) {
			variable_type.kind = FSParser::DataType::VARIANT;
		} else {
			push_error(vformat(R"(Unable to iterate on value of type "%s".)", list_type.to_string()), p_for->list);
		}
	}

	if (p_for->variable) {
		if (p_for->datatype_specifier) {
			FSParser::DataType specified_type = type_from_metatype(resolve_datatype(p_for->datatype_specifier));
			if (!specified_type.is_variant()) {
				if (variable_type.is_variant() || !variable_type.is_hard_type()) {
					mark_node_unsafe(p_for->variable);
					p_for->use_conversion_assign = true;
				} else if (!is_type_compatible(specified_type, variable_type, true, p_for->variable)) {
					if (is_type_compatible(variable_type, specified_type)) {
						mark_node_unsafe(p_for->variable);
						p_for->use_conversion_assign = true;
					} else {
						push_error(vformat(R"(Unable to iterate on value of type "%s" with variable of type "%s".)", list_type.to_string(), specified_type.to_string()), p_for->datatype_specifier);
					}
				} else if (!is_type_compatible(specified_type, variable_type)) {
					p_for->use_conversion_assign = true;
				}
				if (p_for->list) {
					if (p_for->list->type == FSParser::Node::ARRAY) {
						update_array_literal_element_type(static_cast<FSParser::ArrayNode *>(p_for->list), specified_type);
					} else if (p_for->list->type == FSParser::Node::DICTIONARY) {
						update_dictionary_literal_element_type(static_cast<FSParser::DictionaryNode *>(p_for->list), specified_type, FSParser::DataType::get_variant_type());
					}
				}
			}
			p_for->variable->set_datatype(specified_type);
		} else {
			p_for->variable->set_datatype(variable_type);
#ifdef DEBUG_ENABLED
			if (variable_type.is_hard_type()) {
				parser->push_warning(p_for->variable, FSWarning::INFERRED_DECLARATION, R"("for" iterator variable)", p_for->variable->name);
			} else {
				parser->push_warning(p_for->variable, FSWarning::UNTYPED_DECLARATION, R"("for" iterator variable)", p_for->variable->name);
			}
#endif // DEBUG_ENABLED
		}
	}

	HashMap<const FSParser::Node *, FSParser::DataType> previous_flow_narrowed_types = flow_finality.get_flow_narrowed_types();
	resolve_suite(p_for->loop);
	flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;
	p_for->set_datatype(p_for->loop->get_datatype());
#ifdef DEBUG_ENABLED
	if (p_for->variable) {
		is_shadowing(p_for->variable, R"("for" iterator variable)", true);
	}
#endif // DEBUG_ENABLED
}

void FSAnalyzer::resolve_while(FSParser::WhileNode *p_while) {
	flow_finality.reduce_condition_expression(p_while->condition);

	HashMap<const FSParser::Node *, FSParser::DataType> previous_flow_narrowed_types = flow_finality.get_flow_narrowed_types();
	flow_finality.apply_flow_narrowing_from_condition(p_while->condition, true);
	resolve_suite(p_while->loop);
	flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;
	p_while->set_datatype(p_while->loop->get_datatype());
}

void FSAnalyzer::resolve_assert(FSParser::AssertNode *p_assert) {
	flow_finality.reduce_condition_expression(p_assert->condition);
	if (p_assert->message != nullptr) {
		reduce_expression(p_assert->message);
		if (!p_assert->message->get_datatype().has_no_type() && (p_assert->message->get_datatype().kind != FSParser::DataType::BUILTIN || p_assert->message->get_datatype().builtin_type != Variant::STRING)) {
			push_error(R"(Expected string for assert error message.)", p_assert->message);
		}
	}

	p_assert->set_datatype(p_assert->condition->get_datatype());
	flow_finality.apply_flow_narrowing_from_condition(p_assert->condition, true);

#ifdef DEBUG_ENABLED
	if (p_assert->condition->is_constant) {
		if (p_assert->condition->reduced_value.booleanize()) {
			parser->push_warning(p_assert->condition, FSWarning::ASSERT_ALWAYS_TRUE);
		} else if (!(p_assert->condition->type == FSParser::Node::LITERAL && static_cast<FSParser::LiteralNode *>(p_assert->condition)->value.get_type() == Variant::BOOL)) {
			parser->push_warning(p_assert->condition, FSWarning::ASSERT_ALWAYS_FALSE);
		}
	}
#endif // DEBUG_ENABLED
}

void FSAnalyzer::resolve_match(FSParser::MatchNode *p_match) {
	reduce_expression(p_match->test);

	for (int i = 0; i < p_match->branches.size(); i++) {
		resolve_match_branch(p_match->branches[i], p_match->test);

		decide_suite_type(p_match, p_match->branches[i]);
	}

#ifdef DEBUG_ENABLED
	check_match_exhaustiveness(p_match);
#endif
}

#ifdef DEBUG_ENABLED
void FSAnalyzer::check_match_exhaustiveness(FSParser::MatchNode *p_match) {
	if (p_match->test == nullptr) {
		return; // Parse error: `match` with no test expression.
	}
	const FSParser::DataType &match_type = p_match->test->get_datatype();
	if (!match_type.is_set()) {
		return; // Type unknown; cannot classify the domain.
	}

	// A branch counts as a default only with an unguarded wildcard/bind pattern.
	// The parser already clears `has_wildcard` when a guard is present.
	bool has_default = false;
	for (FSParser::MatchBranchNode *branch : p_match->branches) {
		if (branch->has_wildcard) {
			has_default = true;
			break;
		}
	}

	// Classify the matched type's domain.
	// `domain_values` maps each value's display name to its integer value.
	// Iteration order follows insertion order (Godot HashMap), i.e. enum
	// declaration order, so the unhandled list is deterministic.
	bool is_finite_domain = false;
	HashMap<StringName, int64_t> domain_values;
	String type_name;
	// A tagged union also has a finite case domain, but its cases are Array values rather than int
	// constants, so the int-constant coverage analysis below cannot see them. Case-aware
	// exhaustiveness arrives with the tagged-union match patterns.
	if (match_type.kind == FSParser::DataType::ENUM && !match_type.is_tagged_union) {
		is_finite_domain = true;
		domain_values = match_type.enum_values;
		type_name = match_type.enum_type;
	} else if (match_type.kind == FSParser::DataType::BUILTIN && match_type.builtin_type == Variant::BOOL) {
		is_finite_domain = true;
		domain_values[SNAME("false")] = 0;
		domain_values[SNAME("true")] = 1;
		type_name = "bool";
	}

	if (!is_finite_domain) {
		if (!has_default) {
			parser->push_warning(p_match, FSWarning::MATCH_WITHOUT_DEFAULT);
		}
		return;
	}

	if (has_default || domain_values.is_empty()) {
		return; // Exhaustive via default, or nothing to check.
	}

	// `match` compares typeof() before value, so only same-typed constants can
	// cover a value at runtime: INT for enums, BOOL for the bool domain.
	const Variant::Type expected_type = match_type.kind == FSParser::DataType::ENUM ? Variant::INT : Variant::BOOL;

	// For nullable types, `null` (`Variant::NIL`) is a valid runtime value that
	// no enum integer or bool constant can cover, so it must be handled by an
	// explicit `null` pattern (or a wildcard) to be exhaustive.
	const bool domain_includes_null = match_type.is_nullable;

	// Collect values covered by unguarded, statically-constant patterns.
	bool null_covered = false;
	HashSet<int64_t> covered_values;
	for (FSParser::MatchBranchNode *branch : p_match->branches) {
		if (branch->guard_body != nullptr) {
			continue; // Guard may fail; does not guarantee coverage.
		}
		for (FSParser::PatternNode *pattern : branch->patterns) {
			const FSParser::ExpressionNode *value_node = nullptr;
			if (pattern->pattern_type == FSParser::PatternNode::PT_LITERAL) {
				value_node = pattern->literal;
			} else if (pattern->pattern_type == FSParser::PatternNode::PT_EXPRESSION) {
				value_node = pattern->expression;
			} else {
				// Array/dictionary patterns cannot cover enum integers or bool values.
				continue;
			}

			if (value_node == nullptr || !value_node->is_constant) {
				return; // Non-constant pattern: cannot prove coverage; bail out.
			}
			if (value_node->reduced_value.get_type() != expected_type) {
				// A `null` pattern covers the nullable domain's `null` value.
				if (domain_includes_null && value_node->reduced_value.get_type() == Variant::NIL) {
					null_covered = true;
				}
				// A different-typed constant can never match this domain at
				// runtime (match compares typeof() first), so it covers nothing.
				// Skip it — do NOT bail out, the value stays unhandled.
				continue;
			}
			covered_values.insert((int64_t)value_node->reduced_value);
		}
	}

	// Report any domain value with no covering pattern.
	Vector<String> unhandled;
	for (const KeyValue<StringName, int64_t> &E : domain_values) {
		if (!covered_values.has(E.value)) {
			unhandled.push_back(String(E.key));
		}
	}
	if (domain_includes_null && !null_covered) {
		unhandled.push_back("null");
	}

	if (!unhandled.is_empty()) {
		parser->push_warning(p_match, FSWarning::NON_EXHAUSTIVE_MATCH, type_name, String(", ").join(unhandled));
	}
}
#endif // DEBUG_ENABLED

void FSAnalyzer::resolve_match_branch(FSParser::MatchBranchNode *p_match_branch, FSParser::ExpressionNode *p_match_test) {
	// Apply annotations.
	for (FSParser::AnnotationNode *&E : p_match_branch->annotations) {
		resolve_annotation(E);
		E->apply(parser, p_match_branch, nullptr); // TODO: Provide `p_class`.
	}

	for (int i = 0; i < p_match_branch->patterns.size(); i++) {
		resolve_match_pattern(p_match_branch->patterns[i], p_match_test);
	}

	HashMap<const FSParser::Node *, FSParser::DataType> previous_flow_narrowed_types = flow_finality.get_flow_narrowed_types();
	flow_finality.apply_match_branch_flow_narrowing(p_match_test, p_match_branch);

	if (p_match_branch->guard_body) {
		resolve_suite(p_match_branch->guard_body, false);
	}

	resolve_suite(p_match_branch->block);
	flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;

	decide_suite_type(p_match_branch, p_match_branch->block);
}

void FSAnalyzer::resolve_match_pattern(FSParser::PatternNode *p_match_pattern, FSParser::ExpressionNode *p_match_test, const FSParser::DataType *p_match_test_type) {
	if (p_match_pattern == nullptr) {
		return;
	}

	FSParser::DataType result;
	FSParser::DataType match_test_type;
	bool has_match_test_type = false;
	if (p_match_test != nullptr) {
		match_test_type = p_match_test->get_datatype();
		has_match_test_type = match_test_type.is_set();
	} else if (p_match_test_type != nullptr) {
		match_test_type = *p_match_test_type;
		has_match_test_type = match_test_type.is_set();
	}

	switch (p_match_pattern->pattern_type) {
		case FSParser::PatternNode::PT_LITERAL:
			if (p_match_pattern->literal) {
				reduce_literal(p_match_pattern->literal);
				result = p_match_pattern->literal->get_datatype();
			}
			break;
		case FSParser::PatternNode::PT_EXPRESSION:
			if (p_match_pattern->expression) {
				FSParser::ExpressionNode *expr = p_match_pattern->expression;
				reduce_expression(expr);
				result = expr->get_datatype();
				if (!expr->is_constant) {
					bool valid_type_test_pattern = false;
					if (expr->type == FSParser::Node::TYPE_TEST && p_match_test != nullptr && p_match_test->type == FSParser::Node::IDENTIFIER) {
						const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(expr);
						if (type_test->operand != nullptr && type_test->operand->type == FSParser::Node::IDENTIFIER) {
							const FSParser::IdentifierNode *pattern_operand = static_cast<const FSParser::IdentifierNode *>(type_test->operand);
							const FSParser::IdentifierNode *match_identifier = static_cast<const FSParser::IdentifierNode *>(p_match_test);
							valid_type_test_pattern = pattern_operand->name == match_identifier->name;
						}
					}
					if (!valid_type_test_pattern) {
						while (expr && expr->type == FSParser::Node::SUBSCRIPT) {
							FSParser::SubscriptNode *sub = static_cast<FSParser::SubscriptNode *>(expr);
							if (!sub->is_attribute) {
								expr = nullptr;
							} else {
								expr = sub->base;
							}
						}
						if (!expr || (expr->type != FSParser::Node::IDENTIFIER && !result.is_meta_type)) {
							push_error(R"(Expression in match pattern must be a constant expression, an identifier, or an attribute access ("A.B").)", expr);
						}
					}
				}
			}
			break;
		case FSParser::PatternNode::PT_BIND:
			if (has_match_test_type) {
				result = match_test_type;
			} else {
				result = FSParser::DataType::get_variant_type();
			}
			p_match_pattern->bind->set_datatype(result);
#ifdef DEBUG_ENABLED
			is_shadowing(p_match_pattern->bind, "pattern bind", true);
			if (p_match_pattern->bind->usages == 0 && !String(p_match_pattern->bind->name).begins_with("_")) {
				parser->push_warning(p_match_pattern->bind, FSWarning::UNUSED_VARIABLE, p_match_pattern->bind->name);
			}
#endif // DEBUG_ENABLED
			break;
		case FSParser::PatternNode::PT_ARRAY:
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				FSParser::DataType element_type;
				FSParser::DataType *element_type_ptr = nullptr;
				if (has_match_test_type && match_test_type.kind == FSParser::DataType::BUILTIN && match_test_type.builtin_type == Variant::ARRAY && match_test_type.has_container_element_type(0)) {
					element_type = match_test_type.get_container_element_type(0);
					element_type_ptr = &element_type;
				}
				resolve_match_pattern(p_match_pattern->array[i], nullptr, element_type_ptr);
				decide_suite_type(p_match_pattern, p_match_pattern->array[i]);
			}
			result = p_match_pattern->get_datatype();
			break;
		case FSParser::PatternNode::PT_DICTIONARY:
			for (int i = 0; i < p_match_pattern->dictionary.size(); i++) {
				if (p_match_pattern->dictionary[i].key) {
					reduce_expression(p_match_pattern->dictionary[i].key);
					if (!p_match_pattern->dictionary[i].key->is_constant) {
						push_error(R"(Expression in dictionary pattern key must be a constant.)", p_match_pattern->dictionary[i].key);
					}
				}

				if (p_match_pattern->dictionary[i].value_pattern) {
					FSParser::DataType value_type;
					FSParser::DataType *value_type_ptr = nullptr;
					if (has_match_test_type && match_test_type.kind == FSParser::DataType::BUILTIN && match_test_type.builtin_type == Variant::DICTIONARY && match_test_type.has_container_element_type(1)) {
						value_type = match_test_type.get_container_element_type(1);
						value_type_ptr = &value_type;
					}
					resolve_match_pattern(p_match_pattern->dictionary[i].value_pattern, nullptr, value_type_ptr);
					decide_suite_type(p_match_pattern, p_match_pattern->dictionary[i].value_pattern);
				}
			}
			result = p_match_pattern->get_datatype();
			break;
		case FSParser::PatternNode::PT_WILDCARD:
		case FSParser::PatternNode::PT_REST:
			result.kind = FSParser::DataType::VARIANT;
			break;
	}

	p_match_pattern->set_datatype(result);
}

void FSAnalyzer::resolve_return(FSParser::ReturnNode *p_return) {
	FSParser::DataType result;
	bool self_container_literal_validated = false;

	FSParser::DataType expected_type;
	bool has_expected_type = parser->current_function != nullptr;
	if (has_expected_type) {
		expected_type = parser->current_function->get_datatype();
	}
	FSParser::DataType compatibility_expected_type = expected_type;
	if (has_expected_type) {
		const FSParser::DataType current_self_type = _self_type_for_class(parser->current_class);
		compatibility_expected_type = _substitute_self_type_parameter(expected_type, current_self_type);
	}
	const bool preserve_self_contract = has_expected_type && parser->current_class != nullptr &&
			parser->current_function != nullptr &&
			!parser->current_function->is_abstract && _datatype_contains_self_type_parameter(expected_type);

	if (p_return->return_value != nullptr) {
		bool is_void_function = has_expected_type && expected_type.is_hard_type() && expected_type.kind == FSParser::DataType::BUILTIN && expected_type.builtin_type == Variant::NIL;
		bool is_call = p_return->return_value->type == FSParser::Node::CALL;
		if (is_void_function && is_call) {
			// Pretend the call is a root expression to allow those that are "void".
			reduce_call(static_cast<FSParser::CallNode *>(p_return->return_value), false, true);
		} else {
			reduce_expression(p_return->return_value);
		}
		if (is_void_function) {
			p_return->void_return = true;
			const FSParser::DataType &return_type = p_return->return_value->datatype;
			if (is_call && !return_type.is_hard_type()) {
				String function_name = parser->current_function->identifier ? parser->current_function->identifier->name.operator String() : String("<anonymous function>");
				String called_function_name = static_cast<FSParser::CallNode *>(p_return->return_value)->function_name.operator String();
#ifdef DEBUG_ENABLED
				parser->push_warning(p_return, FSWarning::UNSAFE_VOID_RETURN, function_name, called_function_name);
#endif // DEBUG_ENABLED
				mark_node_unsafe(p_return);
			} else if (!is_call) {
				push_error("A void function cannot return a value.", p_return);
			}
			result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
			result.kind = FSParser::DataType::BUILTIN;
			result.builtin_type = Variant::NIL;
			result.is_constant = true;
		} else {
			const FSParser::DataType &literal_expected_type = preserve_self_contract ? expected_type : compatibility_expected_type;
			const int literal_errors_before = parser->get_errors().size();
			if (p_return->return_value->type == FSParser::Node::ARRAY && has_expected_type &&
					literal_expected_type.has_container_element_type(0)) {
				const bool substitute_self_runtime_type = preserve_self_contract &&
						!parser->current_class->is_trait &&
						_datatype_contains_self_type_parameter(literal_expected_type.get_container_element_type(0));
				update_array_literal_element_type(static_cast<FSParser::ArrayNode *>(p_return->return_value),
						literal_expected_type.get_container_element_type(0),
						false,
						substitute_self_runtime_type);
				self_container_literal_validated = substitute_self_runtime_type &&
						parser->get_errors().size() == literal_errors_before;
			} else if (p_return->return_value->type == FSParser::Node::DICTIONARY && has_expected_type &&
					literal_expected_type.has_container_element_types()) {
				const bool substitute_self_runtime_type = preserve_self_contract &&
						!parser->current_class->is_trait &&
						(_datatype_contains_self_type_parameter(literal_expected_type.get_container_element_type_or_variant(0)) ||
								_datatype_contains_self_type_parameter(literal_expected_type.get_container_element_type_or_variant(1)));
				update_dictionary_literal_element_type(static_cast<FSParser::DictionaryNode *>(p_return->return_value),
						literal_expected_type.get_container_element_type_or_variant(0),
						literal_expected_type.get_container_element_type_or_variant(1),
						false,
						substitute_self_runtime_type);
				self_container_literal_validated = substitute_self_runtime_type &&
						parser->get_errors().size() == literal_errors_before;
			}
			if (has_expected_type && compatibility_expected_type.is_hard_type() && p_return->return_value->is_constant) {
				update_const_expression_builtin_type(p_return->return_value, compatibility_expected_type, "return");
			}
			if (has_expected_type) {
				mark_coroutine_handle_capture(p_return->return_value, expected_type);
			}
			result = p_return->return_value->get_datatype();
		}
	} else {
		// Return type is null by default.
		result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::NIL;
		result.is_constant = true;
	}

	if (has_expected_type && !compatibility_expected_type.is_variant()) {
		if (preserve_self_contract) {
			if (!self_container_literal_validated && (!result.is_hard_type() || !_datatype_matches_self_return_contract(expected_type, result))) {
				push_error(vformat(R"(Cannot return value of type "%s" because the function return type is "%s".)",
								   result.to_string(),
								   expected_type.to_string()),
						p_return);
			}
			p_return->set_datatype(result);
			return;
		}
		if (result.is_variant() || !result.is_hard_type()) {
			if (result.is_variant() && strict_dynamic_checks) {
				push_error(vformat(R"(Cannot return Variant value in strict dynamic mode; expected "%s".)",
								   expected_type.to_string()),
						p_return);
			} else {
				mark_node_unsafe(p_return);
			}
			if (!result.is_variant() && !is_type_compatible(compatibility_expected_type, result, true, p_return)) {
				downgrade_node_type_source(p_return);
			}
		} else if (!is_type_compatible(compatibility_expected_type, result, true, p_return)) {
			const bool nullable_mismatch = strict_null_checks && result.is_nullable &&
					!compatibility_expected_type.is_nullable && !compatibility_expected_type.is_variant();
			mark_node_unsafe(p_return);
			if (nullable_mismatch || !is_type_compatible(result, compatibility_expected_type)) {
				if (nullable_mismatch) {
					push_error(vformat(R"(Cannot return nullable value of type "%s"; expected non-nullable "%s".)",
									   result.to_string(),
									   expected_type.to_string()),
							p_return);
				} else {
					push_error(vformat(R"(Cannot return value of type "%s" because the function return type is "%s".)",
									   result.to_string(),
									   expected_type.to_string()),
							p_return);
				}
			}
#ifdef DEBUG_ENABLED
		} else if (expected_type.builtin_type == Variant::INT && result.builtin_type == Variant::FLOAT) {
			parser->push_warning(p_return, FSWarning::NARROWING_CONVERSION);
#endif // DEBUG_ENABLED
		}
	}

	p_return->set_datatype(result);
}

void FSAnalyzer::reduce_expression(FSParser::ExpressionNode *p_expression, bool p_is_root) {
	// This one makes some magic happen.

	if (p_expression == nullptr) {
		return;
	}

	if (p_expression->reduced) {
		// Don't do this more than once.
		return;
	}

	p_expression->reduced = true;

	switch (p_expression->type) {
		case FSParser::Node::ARRAY:
			reduce_array(static_cast<FSParser::ArrayNode *>(p_expression));
			break;
		case FSParser::Node::ASSIGNMENT:
			reduce_assignment(static_cast<FSParser::AssignmentNode *>(p_expression));
			break;
		case FSParser::Node::AWAIT:
			reduce_await(static_cast<FSParser::AwaitNode *>(p_expression));
			break;
		case FSParser::Node::BINARY_OPERATOR:
			reduce_binary_op(static_cast<FSParser::BinaryOpNode *>(p_expression));
			break;
		case FSParser::Node::CALL:
			reduce_call(static_cast<FSParser::CallNode *>(p_expression), false, p_is_root);
			break;
		case FSParser::Node::CAST:
			reduce_cast(static_cast<FSParser::CastNode *>(p_expression));
			break;
		case FSParser::Node::DICTIONARY:
			reduce_dictionary(static_cast<FSParser::DictionaryNode *>(p_expression));
			break;
		case FSParser::Node::GET_NODE:
			reduce_get_node(static_cast<FSParser::GetNodeNode *>(p_expression));
			break;
		case FSParser::Node::IDENTIFIER:
			reduce_identifier(static_cast<FSParser::IdentifierNode *>(p_expression));
			break;
		case FSParser::Node::LAMBDA:
			reduce_lambda(static_cast<FSParser::LambdaNode *>(p_expression));
			break;
		case FSParser::Node::LITERAL:
			reduce_literal(static_cast<FSParser::LiteralNode *>(p_expression));
			break;
		case FSParser::Node::PRELOAD:
			reduce_preload(static_cast<FSParser::PreloadNode *>(p_expression));
			break;
		case FSParser::Node::SELF:
			reduce_self(static_cast<FSParser::SelfNode *>(p_expression));
			break;
		case FSParser::Node::SUBSCRIPT:
			reduce_subscript(static_cast<FSParser::SubscriptNode *>(p_expression));
			break;
		case FSParser::Node::TERNARY_OPERATOR:
			reduce_ternary_op(static_cast<FSParser::TernaryOpNode *>(p_expression), p_is_root);
			break;
		case FSParser::Node::TUPLE_LITERAL:
			reduce_tuple_literal(static_cast<FSParser::TupleLiteralNode *>(p_expression));
			break;
		case FSParser::Node::TYPE_TEST:
			reduce_type_test(static_cast<FSParser::TypeTestNode *>(p_expression));
			break;
		case FSParser::Node::UNARY_OPERATOR:
			reduce_unary_op(static_cast<FSParser::UnaryOpNode *>(p_expression));
			break;
		// Non-expressions. Here only to make sure new nodes aren't forgotten.
		case FSParser::Node::NONE:
		case FSParser::Node::ANNOTATION:
		case FSParser::Node::ANNOTATION_DECLARATION:
		case FSParser::Node::ASSERT:
		case FSParser::Node::BREAK:
		case FSParser::Node::BREAKPOINT:
		case FSParser::Node::CLASS:
		case FSParser::Node::CONFORMANCE:
		case FSParser::Node::CONSTANT:
		case FSParser::Node::CONTINUE:
		case FSParser::Node::ENUM:
		case FSParser::Node::FOR:
		case FSParser::Node::FUNCTION:
		case FSParser::Node::IF:
		case FSParser::Node::MATCH:
		case FSParser::Node::MATCH_BRANCH:
		case FSParser::Node::PARAMETER:
		case FSParser::Node::PASS:
		case FSParser::Node::PATTERN:
		case FSParser::Node::RETURN:
		case FSParser::Node::SIGNAL:
		case FSParser::Node::SUITE:
		case FSParser::Node::TUPLE:
		case FSParser::Node::TYPE:
		case FSParser::Node::TYPE_PARAMETER:
		case FSParser::Node::VARIABLE:
		case FSParser::Node::VARIABLE_DESTRUCTURE:
		case FSParser::Node::WHILE:
			ERR_FAIL_MSG("Reaching unreachable case");
	}

	if (p_expression->get_datatype().kind == FSParser::DataType::UNRESOLVED) {
		// Prevent `is_type_compatible()` errors for incomplete expressions.
		// The error can still occur if `reduce_*()` is called directly.
		FSParser::DataType dummy;
		dummy.kind = FSParser::DataType::VARIANT;
		p_expression->set_datatype(dummy);
	}
}

void FSAnalyzer::reduce_tuple_literal(FSParser::TupleLiteralNode *p_tuple_literal) {
	for (int i = 0; i < p_tuple_literal->elements.size(); i++) {
		reduce_expression(p_tuple_literal->elements[i]);
	}

	// A tuple literal is always unnamed: its type is exactly the inferred element shape. A named
	// tuple is only produced by explicitly calling its declaration.
	Vector<FSParser::DataType> element_types;
	for (int i = 0; i < p_tuple_literal->elements.size(); i++) {
		FSParser::DataType element_type = p_tuple_literal->elements[i]->get_datatype();
		if (!element_type.is_set() || !element_type.is_hard_type()) {
			// A dynamic element keeps the slot open so the tuple stays assignable to any shape
			// that matches in the remaining positions.
			element_type = FSParser::DataType::get_variant_type();
		}
		element_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		element_type.is_constant = false;
		element_type.is_meta_type = false;
		element_types.push_back(element_type);
	}

	p_tuple_literal->set_datatype(make_tuple_type(StringName(), String(), String(), element_types, Vector<StringName>(), false));
}

void FSAnalyzer::reduce_array(FSParser::ArrayNode *p_array) {
	for (int i = 0; i < p_array->elements.size(); i++) {
		FSParser::ExpressionNode *element = p_array->elements[i];
		reduce_expression(element);
	}

	// It's array in any case.
	FSParser::DataType arr_type;
	arr_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	arr_type.kind = FSParser::DataType::BUILTIN;
	arr_type.builtin_type = Variant::ARRAY;
	arr_type.is_constant = true;

	p_array->set_datatype(arr_type);
}

#ifdef DEBUG_ENABLED
static bool enum_has_value(const FSParser::DataType p_type, int64_t p_value) {
	for (const KeyValue<StringName, int64_t> &E : p_type.enum_values) {
		if (E.value == p_value) {
			return true;
		}
	}
	return false;
}
#endif // DEBUG_ENABLED

void FSAnalyzer::update_const_expression_builtin_type(FSParser::ExpressionNode *p_expression, const FSParser::DataType &p_type, const char *p_usage, bool p_is_cast) {
	if (p_expression->get_datatype() == p_type) {
		return;
	}
	if (p_type.kind != FSParser::DataType::BUILTIN && p_type.kind != FSParser::DataType::ENUM) {
		return;
	}

	FSParser::DataType expression_type = p_expression->get_datatype();
	// An int constant may be cast into an int-backed enum, but never into a tagged union.
	bool is_enum_cast = p_is_cast && p_type.kind == FSParser::DataType::ENUM && !p_type.is_meta_type &&
			!p_type.is_tagged_union && expression_type.builtin_type == Variant::INT;
	if (!is_enum_cast && !is_type_compatible(p_type, expression_type, true, p_expression)) {
		push_error(vformat(R"(Cannot %s a value of type "%s" as "%s".)", p_usage, expression_type.to_string(), p_type.to_string()), p_expression);
		return;
	}
	if (p_type.is_variant() &&
			expression_type.is_meta_type && expression_type.kind == FSParser::DataType::CLASS &&
			!expression_type.type_arguments.is_empty()) {
		return;
	}

	if (p_type.is_nullable && p_expression->is_constant && p_expression->reduced_value.get_type() == Variant::NIL) {
		// An explicit null is kept as-is: a nullable target holds null without converting it to the underlying
		// type. Keyed on the reduced value so a null constant typed as Variant is handled too.
		p_expression->set_datatype(p_type);
		return;
	}

	FSParser::DataType value_type = type_from_variant(p_expression->reduced_value, p_expression);
	if (expression_type.is_variant() && !is_enum_cast && !is_type_compatible(p_type, value_type, true, p_expression)) {
		push_error(vformat(R"(Cannot %s a value of type "%s" as "%s".)", p_usage, value_type.to_string(), p_type.to_string()), p_expression);
		return;
	}

#ifdef DEBUG_ENABLED
	if (p_type.kind == FSParser::DataType::ENUM && value_type.builtin_type == Variant::INT && !enum_has_value(p_type, p_expression->reduced_value)) {
		parser->push_warning(p_expression, FSWarning::INT_AS_ENUM_WITHOUT_MATCH, p_usage, p_expression->reduced_value.stringify(), p_type.to_string());
	}
#endif // DEBUG_ENABLED

	if (value_type.builtin_type == p_type.builtin_type) {
		p_expression->set_datatype(p_type);
		return;
	}

	Variant converted_to;
	const Variant *converted_from = &p_expression->reduced_value;
	Callable::CallError call_error;
	Variant::construct(p_type.builtin_type, converted_to, &converted_from, 1, call_error);
	if (call_error.error) {
		push_error(vformat(R"(Failed to convert a value of type "%s" to "%s".)", value_type.to_string(), p_type.to_string()), p_expression);
		return;
	}

#ifdef DEBUG_ENABLED
	if (p_type.builtin_type == Variant::INT && value_type.builtin_type == Variant::FLOAT) {
		parser->push_warning(p_expression, FSWarning::NARROWING_CONVERSION);
	}
#endif // DEBUG_ENABLED

	p_expression->reduced_value = converted_to;
	p_expression->set_datatype(p_type);
}

// When an array literal is stored (or passed as function argument) to a typed context, we then assume the array is typed.
// This function determines which type is that (if any).
void FSAnalyzer::update_array_literal_element_type(FSParser::ArrayNode *p_array,
		const FSParser::DataType &p_element_type,
		bool p_self_parameter_contract,
		bool p_substitute_self_runtime_type) {
	FSParser::DataType expected_type = p_element_type;

	for (int i = 0; i < p_array->elements.size(); i++) {
		FSParser::ExpressionNode *element_node = p_array->elements[i];
		if (expected_type.kind == FSParser::DataType::BUILTIN && expected_type.builtin_type == Variant::ARRAY && expected_type.has_container_element_type(0) && element_node->type == FSParser::Node::ARRAY) {
			update_array_literal_element_type(
					static_cast<FSParser::ArrayNode *>(element_node),
					expected_type.get_container_element_type(0),
					p_self_parameter_contract,
					p_substitute_self_runtime_type);
		} else if (expected_type.kind == FSParser::DataType::BUILTIN && expected_type.builtin_type == Variant::DICTIONARY && expected_type.has_container_element_types() && element_node->type == FSParser::Node::DICTIONARY) {
			update_dictionary_literal_element_type(static_cast<FSParser::DictionaryNode *>(element_node),
					expected_type.get_container_element_type_or_variant(0),
					expected_type.get_container_element_type_or_variant(1),
					p_self_parameter_contract,
					p_substitute_self_runtime_type);
		}
		mark_coroutine_handle_capture(element_node, expected_type);
		if (element_node->is_constant) {
			update_const_expression_builtin_type(element_node, expected_type, "include");
		}
		const FSParser::DataType &actual_type = element_node->get_datatype();
		if (actual_type.has_no_type()) {
			mark_node_unsafe(element_node);
			continue;
		}
		if (actual_type.is_variant()) {
			if (_datatype_contains_self_type_parameter(expected_type)) {
				push_error(vformat(R"(Cannot have an element of type "%s" in an array of type "Array[%s]".)", actual_type.to_string(), expected_type.to_string()), element_node);
				return;
			}
			if (strict_dynamic_checks && !expected_type.is_variant()) {
				push_error(vformat(R"(Cannot include Variant value in array literal for "Array[%s]" in strict dynamic mode.)",
								   expected_type.to_string()),
						element_node);
				return;
			}
			mark_node_unsafe(element_node);
			continue;
		}
		if (!actual_type.is_hard_type()) {
			if (_datatype_contains_self_type_parameter(expected_type)) {
				push_error(vformat(R"(Cannot have an element of type "%s" in an array of type "Array[%s]".)", actual_type.to_string(), expected_type.to_string()), element_node);
				return;
			}
			mark_node_unsafe(element_node);
			continue;
		}
		if (_datatype_contains_self_type_parameter(expected_type)) {
			const bool valid_self_element = p_self_parameter_contract ? _datatype_matches_self_parameter_contract(expected_type, actual_type) : _datatype_matches_self_return_contract(expected_type, actual_type);
			if (!valid_self_element) {
				push_error(vformat(R"(Cannot have an element of type "%s" in an array of type "Array[%s]".)", actual_type.to_string(), expected_type.to_string()), element_node);
				return;
			}
			continue;
		}
		if (!is_type_compatible(expected_type, actual_type, true, p_array)) {
			if (is_type_compatible(actual_type, expected_type)) {
				mark_node_unsafe(element_node);
				continue;
			}
			push_error(vformat(R"(Cannot have an element of type "%s" in an array of type "Array[%s]".)", actual_type.to_string(), expected_type.to_string()), element_node);
			return;
		}
	}

	FSParser::DataType array_type = p_array->get_datatype();
	array_type.set_container_element_type(0,
			(p_self_parameter_contract || p_substitute_self_runtime_type) ? _substitute_self_type_parameter_with_bounds(expected_type) : expected_type);
	p_array->set_datatype(array_type);
}

// When a dictionary literal is stored (or passed as function argument) to a typed context, we then assume the dictionary is typed.
// This function determines which type is that (if any).
void FSAnalyzer::update_dictionary_literal_element_type(FSParser::DictionaryNode *p_dictionary,
		const FSParser::DataType &p_key_element_type,
		const FSParser::DataType &p_value_element_type,
		bool p_self_parameter_contract,
		bool p_substitute_self_runtime_type) {
	FSParser::DataType expected_key_type = p_key_element_type;
	FSParser::DataType expected_value_type = p_value_element_type;

	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		FSParser::ExpressionNode *key_element_node = p_dictionary->elements[i].key;
		if (expected_key_type.kind == FSParser::DataType::BUILTIN && expected_key_type.builtin_type == Variant::ARRAY && expected_key_type.has_container_element_type(0) && key_element_node->type == FSParser::Node::ARRAY) {
			update_array_literal_element_type(
					static_cast<FSParser::ArrayNode *>(key_element_node),
					expected_key_type.get_container_element_type(0),
					p_self_parameter_contract,
					p_substitute_self_runtime_type);
		} else if (expected_key_type.kind == FSParser::DataType::BUILTIN && expected_key_type.builtin_type == Variant::DICTIONARY && expected_key_type.has_container_element_types() && key_element_node->type == FSParser::Node::DICTIONARY) {
			update_dictionary_literal_element_type(static_cast<FSParser::DictionaryNode *>(key_element_node),
					expected_key_type.get_container_element_type_or_variant(0),
					expected_key_type.get_container_element_type_or_variant(1),
					p_self_parameter_contract,
					p_substitute_self_runtime_type);
		}
		mark_coroutine_handle_capture(key_element_node, expected_key_type);
		if (key_element_node->is_constant) {
			update_const_expression_builtin_type(key_element_node, expected_key_type, "include");
		}
		const FSParser::DataType &actual_key_type = key_element_node->get_datatype();
		if (actual_key_type.has_no_type()) {
			mark_node_unsafe(key_element_node);
		} else if (actual_key_type.is_variant()) {
			if (_datatype_contains_self_type_parameter(expected_key_type)) {
				push_error(vformat(R"(Cannot have a key of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_key_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), key_element_node);
				return;
			}
			if (strict_dynamic_checks && !expected_key_type.is_variant()) {
				push_error(vformat("Cannot include Variant value as dictionary key for "
								   "\"Dictionary[%s, %s]\" in strict dynamic mode.",
								   expected_key_type.to_string(),
								   expected_value_type.to_string()),
						key_element_node);
				return;
			}
			mark_node_unsafe(key_element_node);
		} else if (!actual_key_type.is_hard_type()) {
			if (_datatype_contains_self_type_parameter(expected_key_type)) {
				push_error(vformat(R"(Cannot have a key of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_key_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), key_element_node);
				return;
			}
			mark_node_unsafe(key_element_node);
		} else if (_datatype_contains_self_type_parameter(expected_key_type)) {
			const bool valid_self_key = p_self_parameter_contract ? _datatype_matches_self_parameter_contract(expected_key_type, actual_key_type) : _datatype_matches_self_return_contract(expected_key_type, actual_key_type);
			if (!valid_self_key) {
				push_error(vformat(R"(Cannot have a key of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_key_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), key_element_node);
				return;
			}
		} else if (!is_type_compatible(expected_key_type, actual_key_type, true, p_dictionary)) {
			if (is_type_compatible(actual_key_type, expected_key_type)) {
				mark_node_unsafe(key_element_node);
			} else {
				push_error(vformat(R"(Cannot have a key of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_key_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), key_element_node);
				return;
			}
		}

		FSParser::ExpressionNode *value_element_node = p_dictionary->elements[i].value;
		if (expected_value_type.kind == FSParser::DataType::BUILTIN && expected_value_type.builtin_type == Variant::ARRAY && expected_value_type.has_container_element_type(0) && value_element_node->type == FSParser::Node::ARRAY) {
			update_array_literal_element_type(
					static_cast<FSParser::ArrayNode *>(value_element_node),
					expected_value_type.get_container_element_type(0),
					p_self_parameter_contract,
					p_substitute_self_runtime_type);
		} else if (expected_value_type.kind == FSParser::DataType::BUILTIN && expected_value_type.builtin_type == Variant::DICTIONARY && expected_value_type.has_container_element_types() && value_element_node->type == FSParser::Node::DICTIONARY) {
			update_dictionary_literal_element_type(static_cast<FSParser::DictionaryNode *>(value_element_node),
					expected_value_type.get_container_element_type_or_variant(0),
					expected_value_type.get_container_element_type_or_variant(1),
					p_self_parameter_contract,
					p_substitute_self_runtime_type);
		}
		mark_coroutine_handle_capture(value_element_node, expected_value_type);
		if (value_element_node->is_constant) {
			update_const_expression_builtin_type(value_element_node, expected_value_type, "include");
		}
		const FSParser::DataType &actual_value_type = value_element_node->get_datatype();
		if (actual_value_type.has_no_type()) {
			mark_node_unsafe(value_element_node);
		} else if (actual_value_type.is_variant()) {
			if (_datatype_contains_self_type_parameter(expected_value_type)) {
				push_error(vformat(R"(Cannot have a value of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_value_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), value_element_node);
				return;
			}
			if (strict_dynamic_checks && !expected_value_type.is_variant()) {
				push_error(vformat(R"(Cannot include Variant value as dictionary value for "Dictionary[%s, %s]" in strict dynamic mode.)",
								   expected_key_type.to_string(),
								   expected_value_type.to_string()),
						value_element_node);
				return;
			}
			mark_node_unsafe(value_element_node);
		} else if (!actual_value_type.is_hard_type()) {
			if (_datatype_contains_self_type_parameter(expected_value_type)) {
				push_error(vformat(R"(Cannot have a value of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_value_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), value_element_node);
				return;
			}
			mark_node_unsafe(value_element_node);
		} else if (_datatype_contains_self_type_parameter(expected_value_type)) {
			const bool valid_self_value = p_self_parameter_contract ? _datatype_matches_self_parameter_contract(expected_value_type, actual_value_type) : _datatype_matches_self_return_contract(expected_value_type, actual_value_type);
			if (!valid_self_value) {
				push_error(vformat(R"(Cannot have a value of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_value_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), value_element_node);
				return;
			}
		} else if (!is_type_compatible(expected_value_type, actual_value_type, true, p_dictionary)) {
			if (is_type_compatible(actual_value_type, expected_value_type)) {
				mark_node_unsafe(value_element_node);
			} else {
				push_error(vformat(R"(Cannot have a value of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_value_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), value_element_node);
				return;
			}
		}
	}

	FSParser::DataType dictionary_type = p_dictionary->get_datatype();
	dictionary_type.set_container_element_type(0,
			(p_self_parameter_contract || p_substitute_self_runtime_type) ? _substitute_self_type_parameter_with_bounds(expected_key_type) : expected_key_type);
	dictionary_type.set_container_element_type(1,
			(p_self_parameter_contract || p_substitute_self_runtime_type) ? _substitute_self_type_parameter_with_bounds(expected_value_type) : expected_value_type);
	p_dictionary->set_datatype(dictionary_type);
}

void FSAnalyzer::reduce_assignment(FSParser::AssignmentNode *p_assignment) {
	reduce_expression(p_assignment->assigned_value);

#ifdef DEBUG_ENABLED
	// Increment assignment count for local variables.
	// Before we reduce the assignee because we don't want to warn about not being assigned when performing the assignment.
	if (p_assignment->assignee->type == FSParser::Node::IDENTIFIER) {
		FSParser::IdentifierNode *id = static_cast<FSParser::IdentifierNode *>(p_assignment->assignee);
		if (id->source == FSParser::IdentifierNode::LOCAL_VARIABLE && id->variable_source) {
			id->variable_source->assignments++;
		}
	}
#endif // DEBUG_ENABLED

	flow_finality.clear_flow_narrowing(p_assignment->assignee);
	reduce_expression(p_assignment->assignee);

#ifdef DEBUG_ENABLED
	{
		bool is_subscript = false;
		FSParser::ExpressionNode *base = p_assignment->assignee;
		while (base && base->type == FSParser::Node::SUBSCRIPT) {
			is_subscript = true;
			base = static_cast<FSParser::SubscriptNode *>(base)->base;
		}
		if (base && base->type == FSParser::Node::IDENTIFIER) {
			FSParser::IdentifierNode *id = static_cast<FSParser::IdentifierNode *>(base);
			if (current_lambda && current_lambda->captures_indices.has(id->name)) {
				bool need_warn = false;
				if (is_subscript) {
					const FSParser::DataType &id_type = id->datatype;
					if (id_type.is_hard_type()) {
						switch (id_type.kind) {
							case FSParser::DataType::BUILTIN:
								// TODO: Change `Variant::is_type_shared()` to include packed arrays?
								need_warn = !Variant::is_type_shared(id_type.builtin_type) && id_type.builtin_type < Variant::PACKED_BYTE_ARRAY;
								break;
							case FSParser::DataType::ENUM:
								need_warn = true;
								break;
							default:
								break;
						}
					}
				} else {
					need_warn = true;
				}
				if (need_warn) {
					parser->push_warning(p_assignment, FSWarning::CONFUSABLE_CAPTURE_REASSIGNMENT, id->name);
				}
			}
		}
	}
#endif // DEBUG_ENABLED

	if (p_assignment->assigned_value == nullptr || p_assignment->assignee == nullptr) {
		return;
	}

	FSParser::DataType assignee_type = p_assignment->assignee->get_datatype();

	mark_coroutine_handle_capture(p_assignment->assigned_value, assignee_type);

	if (p_assignment->assignee->type == FSParser::Node::SUBSCRIPT) {
		// Tuples are immutable, so no element write is legal: `t.0 = v`, `t.x = v` and `t[0] = v`
		// are all rejected here rather than degrading to the generic read-only diagnostic.
		const FSParser::SubscriptNode *assignee_subscript = static_cast<FSParser::SubscriptNode *>(p_assignment->assignee);
		if (assignee_subscript->base != nullptr) {
			const FSParser::DataType base_type = assignee_subscript->base->get_datatype();
			if (base_type.is_set() && base_type.kind == FSParser::DataType::TUPLE && !base_type.is_meta_type) {
				push_error(vformat(R"(Cannot assign to an element of tuple "%s"; tuples are immutable.)", base_type.to_string()),
						p_assignment->assignee);
				return;
			}
		}
	}

	if (assignee_type.is_constant) {
		push_error("Cannot assign a new value to a constant.", p_assignment->assignee);
		return;
	} else if (p_assignment->assignee->type == FSParser::Node::SUBSCRIPT && static_cast<FSParser::SubscriptNode *>(p_assignment->assignee)->base->is_constant) {
		const FSParser::DataType &base_type = static_cast<FSParser::SubscriptNode *>(p_assignment->assignee)->base->datatype;
		if (base_type.kind != FSParser::DataType::SCRIPT && base_type.kind != FSParser::DataType::CLASS) { // Static variables.
			push_error("Cannot assign a new value to a constant.", p_assignment->assignee);
			return;
		}
	} else if (assignee_type.is_read_only) {
		push_error("Cannot assign a new value to a read-only property.", p_assignment->assignee);
		return;
	} else if (p_assignment->assignee->type == FSParser::Node::SUBSCRIPT) {
		FSParser::SubscriptNode *sub = static_cast<FSParser::SubscriptNode *>(p_assignment->assignee);
		while (sub) {
			const FSParser::DataType &base_type = sub->base->datatype;
			if (base_type.is_hard_type() && base_type.is_read_only) {
				if (base_type.kind == FSParser::DataType::BUILTIN && !Variant::is_type_shared(base_type.builtin_type)) {
					push_error("Cannot assign a new value to a read-only property.", p_assignment->assignee);
					return;
				}
			} else {
				break;
			}
			if (sub->base->type == FSParser::Node::SUBSCRIPT) {
				sub = static_cast<FSParser::SubscriptNode *>(sub->base);
			} else {
				sub = nullptr;
			}
		}
	}

	// Check if assigned value is an array/dictionary literal, so we can make it a typed container too if appropriate.
	if (p_assignment->assigned_value->type == FSParser::Node::ARRAY && assignee_type.is_hard_type() && assignee_type.has_container_element_type(0)) {
		update_array_literal_element_type(static_cast<FSParser::ArrayNode *>(p_assignment->assigned_value), assignee_type.get_container_element_type(0));
	} else if (p_assignment->assigned_value->type == FSParser::Node::DICTIONARY && assignee_type.is_hard_type() && assignee_type.has_container_element_types()) {
		update_dictionary_literal_element_type(static_cast<FSParser::DictionaryNode *>(p_assignment->assigned_value),
				assignee_type.get_container_element_type_or_variant(0), assignee_type.get_container_element_type_or_variant(1));
	}

	if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE && assignee_type.is_hard_type() && p_assignment->assigned_value->is_constant) {
		update_const_expression_builtin_type(p_assignment->assigned_value, assignee_type, "assign");
	}

	FSParser::DataType assigned_value_type = p_assignment->assigned_value->get_datatype();
	const String assignee_name = identifier_name_from_expression(p_assignment->assignee);

	if (p_assignment->assignee->type == FSParser::Node::SUBSCRIPT) {
		FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(p_assignment->assignee);
		const FSParser::DataType &base_type = subscript->base->get_datatype();
		if (base_type.kind == FSParser::DataType::BUILTIN && base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_types()) {
			if (subscript->index != nullptr && base_type.has_container_element_type(0)) {
				const FSParser::DataType &key_type = base_type.get_container_element_type(0);
				const FSParser::DataType &index_type = subscript->index->get_datatype();
				if (index_type.is_variant()) {
					mark_node_unsafe(subscript->index);
					if (strict_dynamic_checks && !key_type.is_variant()) {
						push_error(vformat(R"(Cannot use key of type "%s" in a dictionary of type "%s".)", index_type.to_string(), base_type.to_string()), subscript->index);
						return;
					}
				}
			}

			if (base_type.has_container_element_type(1) && assigned_value_type.is_variant()) {
				const FSParser::DataType &value_type = base_type.get_container_element_type(1);
				mark_node_unsafe(p_assignment->assigned_value);
				if (strict_dynamic_checks && !value_type.is_variant()) {
					push_error(vformat(R"(Cannot assign a value of type "%s" to a dictionary value of type "%s".)", assigned_value_type.to_string(), value_type.to_string()), p_assignment->assigned_value);
					return;
				}
			}
		}
	}

	bool assignee_is_variant = assignee_type.is_variant();
	bool assignee_is_hard = assignee_type.is_hard_type();
	bool assigned_is_variant = assigned_value_type.is_variant();
	bool assigned_is_hard = assigned_value_type.is_hard_type();
	bool compatible = true;
	bool downgrades_assignee = false;
	bool downgrades_assigned = false;
	FSParser::DataType op_type = assigned_value_type;
	if (p_assignment->operation != FSParser::AssignmentNode::OP_NONE && !op_type.is_variant()) {
		op_type = get_operation_type(p_assignment->variant_op, assignee_type, assigned_value_type, compatible, p_assignment->assigned_value);

		if (assignee_is_variant) {
			// variant assignee
			mark_node_unsafe(p_assignment);
		} else if (!compatible) {
			// incompatible hard types and non-variant assignee
			mark_node_unsafe(p_assignment);
			if (assigned_is_variant) {
				// incompatible hard non-variant assignee and hard variant assigned
				p_assignment->use_conversion_assign = true;
			} else {
				// incompatible hard non-variant types
				push_error(vformat(R"(Invalid operands "%s" and "%s" for assignment operator.)", assignee_type.to_string(), assigned_value_type.to_string()), p_assignment);
			}
		} else if (op_type.type_source == FSParser::DataType::UNDETECTED && !assigned_is_variant) {
			// incompatible non-variant types (at least one weak)
			downgrades_assignee = !assignee_is_hard;
			downgrades_assigned = !assigned_is_hard;
		}
	}
	p_assignment->set_datatype(op_type);

	if (assignee_is_variant) {
		if (!assignee_is_hard) {
			// weak variant assignee
			mark_node_unsafe(p_assignment);
		}
	} else {
		if (_datatype_contains_self_type_parameter(assignee_type)) {
			if (!op_type.is_hard_type() || !_datatype_matches_self_return_contract(assignee_type, op_type)) {
				mark_node_unsafe(p_assignment);
				push_error(vformat(R"(Value of type "%s" cannot be assigned to a variable of type "%s".)",
								   assigned_value_type.to_string(),
								   assignee_type.to_string()),
						p_assignment->assigned_value);
			}
		} else if (assignee_is_hard && !assigned_is_hard) {
			// hard non-variant assignee and weak assigned
			mark_node_unsafe(p_assignment);
			p_assignment->use_conversion_assign = true;
			downgrades_assigned = downgrades_assigned || (!assigned_is_variant && !is_type_compatible(assignee_type, op_type, true, p_assignment->assigned_value));
		} else if (compatible) {
			if (op_type.is_variant()) {
				// non-variant assignee and variant result
				mark_node_unsafe(p_assignment);
				if (strict_dynamic_checks) {
					if (!assignee_name.is_empty()) {
						push_error(vformat(R"(Cannot assign Variant value to variable "%s" in strict dynamic mode; expected "%s".)",
										   assignee_name,
										   assignee_type.to_string()),
								p_assignment->assigned_value);
					} else {
						push_error(vformat(R"(Cannot assign Variant value to target in strict dynamic mode; expected "%s".)",
										   assignee_type.to_string()),
								p_assignment->assigned_value);
					}
				} else if (assignee_is_hard) {
					// hard non-variant assignee and variant result
					p_assignment->use_conversion_assign = true;
				} else {
					// weak non-variant assignee and variant result
					downgrades_assignee = true;
				}
			} else if (!is_type_compatible(assignee_type, op_type, assignee_is_hard, p_assignment->assigned_value)) {
				// non-variant assignee and incompatible result
				mark_node_unsafe(p_assignment);
				if (assignee_is_hard) {
					const bool nullable_mismatch = strict_null_checks && op_type.is_nullable && !assignee_type.is_nullable && !assignee_type.is_variant();
					String type_handle_error;
					if (!nullable_mismatch) {
						type_handle_error = _make_type_handle_assignment_error(
								assignee_type,
								assigned_value_type,
								p_assignment->assigned_value,
								"variable",
								assignee_name,
								false);
					}
					if (!type_handle_error.is_empty()) {
						push_error(type_handle_error, p_assignment->assigned_value);
					} else if (!nullable_mismatch && FSTypeCompatibility::allows_runtime_narrowing(assignee_type, op_type)) {
						// hard non-variant assignee and maybe compatible result
						p_assignment->use_conversion_assign = true;
					} else {
						// hard non-variant assignee and incompatible result
						if (nullable_mismatch) {
							if (!assignee_name.is_empty()) {
								push_error(vformat(R"(Cannot assign nullable value of type "%s" to variable "%s"; expected non-nullable "%s".)",
												   assigned_value_type.to_string(),
												   assignee_name,
												   assignee_type.to_string()),
										p_assignment->assigned_value);
							} else {
								push_error(vformat(R"(Cannot assign nullable value of type "%s" to target; expected non-nullable "%s".)",
												   assigned_value_type.to_string(),
												   assignee_type.to_string()),
										p_assignment->assigned_value);
							}
						} else {
							push_error(vformat(R"(Value of type "%s" cannot be assigned to a variable of type "%s".)",
											   assigned_value_type.to_string(),
											   assignee_type.to_string()),
									p_assignment->assigned_value);
						}
					}
				} else {
					// weak non-variant assignee and incompatible result
					downgrades_assignee = true;
				}
			} else if ((assignee_type.has_container_element_type(0) && !op_type.has_container_element_type(0)) || (assignee_type.has_container_element_type(1) && !op_type.has_container_element_type(1))) {
				// Typed assignee and untyped result.
				mark_node_unsafe(p_assignment);
			}
		}
	}

	if (downgrades_assignee) {
		downgrade_node_type_source(p_assignment->assignee);
	}
	if (downgrades_assigned) {
		downgrade_node_type_source(p_assignment->assigned_value);
	}

#ifdef DEBUG_ENABLED
	if (assignee_type.is_hard_type() && assignee_type.builtin_type == Variant::INT && assigned_value_type.builtin_type == Variant::FLOAT) {
		parser->push_warning(p_assignment->assigned_value, FSWarning::NARROWING_CONVERSION);
	}
	// Check for assignment with operation before assignment.
	if (p_assignment->operation != FSParser::AssignmentNode::OP_NONE && p_assignment->assignee->type == FSParser::Node::IDENTIFIER) {
		FSParser::IdentifierNode *id = static_cast<FSParser::IdentifierNode *>(p_assignment->assignee);
		// Use == 1 here because this assignment was already counted in the beginning of the function.
		if (id->source == FSParser::IdentifierNode::LOCAL_VARIABLE && id->variable_source && id->variable_source->assignments == 1) {
			parser->push_warning(p_assignment, FSWarning::UNASSIGNED_VARIABLE_OP_ASSIGN, id->name, Variant::get_operator_name(p_assignment->variant_op));
		}
	}
#endif // DEBUG_ENABLED
}

void FSAnalyzer::reduce_await(FSParser::AwaitNode *p_await) {
	if (p_await->to_await == nullptr) {
		FSParser::DataType await_type;
		await_type.kind = FSParser::DataType::VARIANT;
		p_await->set_datatype(await_type);
		return;
	}

	if (p_await->to_await->type == FSParser::Node::CALL) {
		reduce_call(static_cast<FSParser::CallNode *>(p_await->to_await), true);
	} else {
		reduce_expression(p_await->to_await);
	}

	FSParser::DataType operand_type = p_await->to_await->get_datatype();
	FSParser::DataType await_type = operand_type;
	if (operand_type.is_coroutine) {
		// Awaiting a Coroutine[T] yields T from container_element_types[0]; a coroutine without a
		// recorded result type (e.g. a bare AsyncCallable.call()) unwraps to Variant. This is a
		// single-level unwrap: await Coroutine[Coroutine[U]] yields Coroutine[U].
		if (operand_type.has_container_element_type(0)) {
			await_type = operand_type.get_container_element_type(0);
			if (operand_type.is_nullable && !await_type.is_variant() &&
					!(await_type.kind == FSParser::DataType::BUILTIN && await_type.builtin_type == Variant::NIL)) {
				// Awaiting a nullable coroutine can observe a null handle (`await null` yields null at
				// runtime), so the awaited result is nullable too.
				await_type.is_nullable = true;
			}
		} else {
			await_type = FSParser::DataType();
			await_type.kind = FSParser::DataType::VARIANT;
		}
	} else if (operand_type.is_hard_type() && operand_type.kind == FSParser::DataType::BUILTIN && operand_type.builtin_type == Variant::SIGNAL) {
		// We cannot infer the type of the result of waiting for a signal.
		await_type.kind = FSParser::DataType::VARIANT;
		await_type.type_source = FSParser::DataType::UNDETECTED;
	} else if (p_await->to_await->is_constant) {
		p_await->is_constant = p_await->to_await->is_constant;
		p_await->reduced_value = p_await->to_await->reduced_value;
		// Awaiting a plain value yields it unchanged; a non-coroutine operand never carries the flag.
		await_type.is_coroutine = false;
	}
	// The coroutine branch keeps the unwrapped result's own flag, so awaiting Coroutine[Coroutine[U]]
	// correctly stays a Coroutine[U]; only the non-coroutine branches above can leave a stale flag.
	p_await->set_datatype(await_type);

#ifdef DEBUG_ENABLED
	FSParser::DataType to_await_type = p_await->to_await->get_datatype();
	if (!to_await_type.is_coroutine && !to_await_type.is_variant() && to_await_type.builtin_type != Variant::SIGNAL) {
		parser->push_warning(p_await, FSWarning::REDUNDANT_AWAIT);
	}
#endif // DEBUG_ENABLED
}

void FSAnalyzer::reduce_binary_op(FSParser::BinaryOpNode *p_binary_op) {
	reduce_expression(p_binary_op->left_operand);
	reduce_expression(p_binary_op->right_operand);

	FSParser::DataType left_type;
	if (p_binary_op->left_operand) {
		left_type = p_binary_op->left_operand->get_datatype();
	}
	FSParser::DataType right_type;
	if (p_binary_op->right_operand) {
		right_type = p_binary_op->right_operand->get_datatype();
	}

	if (!left_type.is_set() || !right_type.is_set()) {
		return;
	}

#ifdef DEBUG_ENABLED
	if (p_binary_op->variant_op == Variant::OP_DIVIDE &&
			(left_type.builtin_type == Variant::INT ||
					left_type.builtin_type == Variant::VECTOR2I ||
					left_type.builtin_type == Variant::VECTOR3I ||
					left_type.builtin_type == Variant::VECTOR4I) &&
			(right_type.builtin_type == Variant::INT ||
					right_type.builtin_type == left_type.builtin_type)) {
		parser->push_warning(p_binary_op, FSWarning::INTEGER_DIVISION);
	}
#endif // DEBUG_ENABLED

	if (p_binary_op->left_operand->is_constant && p_binary_op->right_operand->is_constant) {
		p_binary_op->is_constant = true;
		if (p_binary_op->variant_op < Variant::OP_MAX) {
			bool valid = false;
			Variant::evaluate(p_binary_op->variant_op, p_binary_op->left_operand->reduced_value, p_binary_op->right_operand->reduced_value, p_binary_op->reduced_value, valid);
			if (!valid) {
				if (p_binary_op->reduced_value.get_type() == Variant::STRING) {
					push_error(vformat(R"(%s in operator %s.)", p_binary_op->reduced_value, Variant::get_operator_name(p_binary_op->variant_op)), p_binary_op);
				} else {
					push_error(vformat(R"(Invalid operands to operator %s, %s and %s.)",
									   Variant::get_operator_name(p_binary_op->variant_op),
									   Variant::get_type_name(p_binary_op->left_operand->reduced_value.get_type()),
									   Variant::get_type_name(p_binary_op->right_operand->reduced_value.get_type())),
							p_binary_op);
				}
			}
		} else {
			ERR_PRINT("Parser bug: unknown binary operation.");
		}
		p_binary_op->set_datatype(type_from_variant(p_binary_op->reduced_value, p_binary_op));

		return;
	}

	FSParser::DataType result;

	if ((p_binary_op->variant_op == Variant::OP_EQUAL || p_binary_op->variant_op == Variant::OP_NOT_EQUAL) &&
			((left_type.kind == FSParser::DataType::BUILTIN && left_type.builtin_type == Variant::NIL) || (right_type.kind == FSParser::DataType::BUILTIN && right_type.builtin_type == Variant::NIL))) {
		// "==" and "!=" operators always return a boolean when comparing to null.
		result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::BOOL;
	} else if (p_binary_op->variant_op == Variant::OP_MODULE && left_type.builtin_type == Variant::STRING) {
		// The modulo operator (%) on string acts as formatting and will always return a string.
		result.type_source = left_type.type_source;
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::STRING;
	} else if (left_type.is_variant() || right_type.is_variant()) {
		// Cannot infer type because one operand can be anything.
		result.kind = FSParser::DataType::VARIANT;
		if (strict_dynamic_checks) {
			push_error(vformat(R"*(Cannot use dynamic operand for "%s" operator in strict dynamic mode.)*", Variant::get_operator_name(p_binary_op->variant_op)), p_binary_op);
		} else {
			mark_node_unsafe(p_binary_op);
		}
	} else if (p_binary_op->variant_op < Variant::OP_MAX) {
		bool valid = false;
		result = get_operation_type(p_binary_op->variant_op, left_type, right_type, valid, p_binary_op);
		if (!valid) {
			const FSParser::DataType &union_type = left_type.is_tagged_union_type() && !left_type.is_meta_type ? left_type : right_type;
			if (union_type.is_tagged_union_type() && !union_type.is_meta_type) {
				push_error(vformat(R"*(Operator "%s" is not available on tagged union "%s"; its cases carry payloads, so its values are not integers. Match on the case first.)*",
								   Variant::get_operator_name(p_binary_op->variant_op), union_type.enum_type),
						p_binary_op);
			} else {
				push_error(vformat(R"(Invalid operands "%s" and "%s" for "%s" operator.)", left_type.to_string(), right_type.to_string(), Variant::get_operator_name(p_binary_op->variant_op)), p_binary_op);
			}
		} else if (!result.is_hard_type()) {
			mark_node_unsafe(p_binary_op);
		}
	} else {
		ERR_PRINT("Parser bug: unknown binary operation.");
	}

	p_binary_op->set_datatype(result);
}

void FSAnalyzer::reduce_call(FSParser::CallNode *p_call, bool p_is_await, bool p_is_root) {
	bool all_is_constant = true;
	HashMap<int, FSParser::ArrayNode *> arrays; // For array literal to potentially type when passing.
	HashMap<int, FSParser::DictionaryNode *> dictionaries; // Same, but for dictionaries.
	for (int i = 0; i < p_call->arguments.size(); i++) {
		reduce_expression(p_call->arguments[i]);
		if (p_call->arguments[i]->type == FSParser::Node::ARRAY) {
			arrays[i] = static_cast<FSParser::ArrayNode *>(p_call->arguments[i]);
		} else if (p_call->arguments[i]->type == FSParser::Node::DICTIONARY) {
			dictionaries[i] = static_cast<FSParser::DictionaryNode *>(p_call->arguments[i]);
		}
		all_is_constant = all_is_constant && p_call->arguments[i]->is_constant;
	}

	Finally clear_captured_flow_narrowing_after_call([&]() {
		flow_finality.clear_captured_flow_narrowing();
	});

	FSParser::Node::Type callee_type = p_call->get_callee_type();
	FSParser::DataType call_type;

	if (!p_call->is_super && callee_type == FSParser::Node::IDENTIFIER) {
		// Call to name directly.
		StringName function_name = p_call->function_name;

#ifdef DEBUG_ENABLED
		const auto warn_return_value_discarded = [&]() {
			const FSParser::DataType &return_type = p_call->get_datatype();
			if (p_is_root && return_type.kind != FSParser::DataType::UNRESOLVED && return_type.builtin_type != Variant::NIL && !return_type.is_coroutine) {
				parser->push_warning(p_call, FSWarning::RETURN_VALUE_DISCARDED, p_call->function_name);
			}
		};
#endif // DEBUG_ENABLED

		if (function_name == SNAME("Object")) {
			push_error(R"*(Invalid constructor "Object()", use "Object.new()" instead.)*", p_call);
			p_call->set_datatype(call_type);
			return;
		}

		Variant::Type builtin_type = FSParser::get_builtin_type(function_name);
		// Builtin constructors and engine utility functions are not FoundryScript functions, so they
		// cannot accept named arguments. Reject them before these paths return.
		if (builtin_type < Variant::VARIANT_MAX || FSUtilityFunctions::function_exists(function_name) || Variant::has_utility_function(function_name)) {
			call_site_validation.reject_named_call_arguments(p_call);
		}
		if (builtin_type < Variant::VARIANT_MAX) {
			// Is a builtin constructor.
			call_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
			call_type.kind = FSParser::DataType::BUILTIN;
			call_type.builtin_type = builtin_type;

			bool safe_to_fold = true;
			switch (builtin_type) {
				// Those are stored by reference so not suited for compile-time construction.
				// Because in this case they would be the same reference in all constructed values.
				case Variant::OBJECT:
				case Variant::DICTIONARY:
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
					safe_to_fold = false;
					break;
				default:
					break;
			}

			if (all_is_constant && safe_to_fold) {
				// Construct here.
				Vector<const Variant *> args;
				for (int i = 0; i < p_call->arguments.size(); i++) {
					args.push_back(&(p_call->arguments[i]->reduced_value));
				}

				Callable::CallError err;
				Variant value;
				Variant::construct(builtin_type, value, (const Variant **)args.ptr(), args.size(), err);

				switch (err.error) {
					case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
						push_error(vformat(R"*(Invalid argument for "%s()" constructor: argument %d should be "%s" but is "%s".)*", Variant::get_type_name(builtin_type), err.argument + 1,
										   Variant::get_type_name(Variant::Type(err.expected)), p_call->arguments[err.argument]->get_datatype().to_string()),
								p_call->arguments[err.argument]);
						break;
					case Callable::CallError::CALL_ERROR_INVALID_METHOD: {
						String signature = Variant::get_type_name(builtin_type) + "(";
						for (int i = 0; i < p_call->arguments.size(); i++) {
							if (i > 0) {
								signature += ", ";
							}
							signature += p_call->arguments[i]->get_datatype().to_string();
						}
						signature += ")";
						push_error(vformat(R"(No constructor of "%s" matches the signature "%s".)", Variant::get_type_name(builtin_type), signature), p_call->callee);
					} break;
					case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
						push_error(vformat(R"*(Too many arguments for "%s()" constructor. Received %d but expected %d.)*", Variant::get_type_name(builtin_type), p_call->arguments.size(), err.expected), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
						push_error(vformat(R"*(Too few arguments for "%s()" constructor. Received %d but expected %d.)*", Variant::get_type_name(builtin_type), p_call->arguments.size(), err.expected), p_call);
						break;
					case Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL:
					case Callable::CallError::CALL_ERROR_METHOD_NOT_CONST:
						break; // Can't happen in a builtin constructor.
					case Callable::CallError::CALL_OK:
						p_call->is_constant = true;
						p_call->reduced_value = value;
						break;
				}
			} else {
				// If there's one argument, try to use copy constructor (those aren't explicitly defined).
				if (p_call->arguments.size() == 1) {
					FSParser::DataType arg_type = p_call->arguments[0]->get_datatype();
					if (arg_type.is_hard_type() && !arg_type.is_variant()) {
						if (arg_type.kind == FSParser::DataType::BUILTIN && arg_type.builtin_type == builtin_type) {
							// Okay.
							p_call->set_datatype(call_type);
#ifdef DEBUG_ENABLED
							warn_return_value_discarded();
#endif // DEBUG_ENABLED
							return;
						}
					} else {
#ifdef DEBUG_ENABLED
						mark_node_unsafe(p_call);
						// Constructors support overloads.
						Vector<String> types;
						for (int i = 0; i < Variant::VARIANT_MAX; i++) {
							if (i != builtin_type && Variant::can_convert_strict((Variant::Type)i, builtin_type)) {
								types.push_back(Variant::get_type_name((Variant::Type)i));
							}
						}
						String expected_types = function_name;
						if (types.size() == 1) {
							expected_types += "\" or \"" + types[0];
						} else if (types.size() >= 2) {
							for (int i = 0; i < types.size() - 1; i++) {
								expected_types += "\", \"" + types[i];
							}
							expected_types += "\", or \"" + types[types.size() - 1];
						}
						parser->push_warning(p_call->arguments[0], FSWarning::UNSAFE_CALL_ARGUMENT, "1", "constructor", function_name, expected_types, "Variant");
#endif // DEBUG_ENABLED
						p_call->set_datatype(call_type);
#ifdef DEBUG_ENABLED
						warn_return_value_discarded();
#endif // DEBUG_ENABLED
						return;
					}
				}

				List<MethodInfo> constructors;
				Variant::get_constructor_list(builtin_type, &constructors);
				bool match = false;

				for (const MethodInfo &info : constructors) {
					if (p_call->arguments.size() < info.arguments.size() - info.default_arguments.size()) {
						continue;
					}
					if (p_call->arguments.size() > info.arguments.size()) {
						continue;
					}

					bool types_match = true;

					for (int64_t i = 0; i < p_call->arguments.size(); ++i) {
						FSParser::DataType par_type = type_from_property(info.arguments[i], true);
						FSParser::DataType arg_type = p_call->arguments[i]->get_datatype();
						if (!is_type_compatible(par_type, arg_type, true)) {
							types_match = false;
							break;
#ifdef DEBUG_ENABLED
						} else {
							if (par_type.builtin_type == Variant::INT && arg_type.builtin_type == Variant::FLOAT && builtin_type != Variant::INT) {
								parser->push_warning(p_call, FSWarning::NARROWING_CONVERSION, function_name);
							}
#endif // DEBUG_ENABLED
						}
					}

					if (types_match) {
						for (int64_t i = 0; i < p_call->arguments.size(); ++i) {
							FSParser::DataType par_type = type_from_property(info.arguments[i], true);
							if (p_call->arguments[i]->is_constant) {
								update_const_expression_builtin_type(p_call->arguments[i], par_type, "pass");
							}
#ifdef DEBUG_ENABLED
							if (!(par_type.is_variant() && par_type.is_hard_type())) {
								FSParser::DataType arg_type = p_call->arguments[i]->get_datatype();
								if (arg_type.is_variant() || !arg_type.is_hard_type()) {
									mark_node_unsafe(p_call);
									parser->push_warning(p_call->arguments[i], FSWarning::UNSAFE_CALL_ARGUMENT, itos(i + 1), "constructor", function_name, par_type.to_string(), arg_type.to_string_strict());
								}
							}
#endif // DEBUG_ENABLED
						}
						match = true;
						call_type = type_from_property(info.return_val);
						break;
					}
				}

				if (!match) {
					String signature = Variant::get_type_name(builtin_type) + "(";
					for (int i = 0; i < p_call->arguments.size(); i++) {
						if (i > 0) {
							signature += ", ";
						}
						signature += p_call->arguments[i]->get_datatype().to_string();
					}
					signature += ")";
					push_error(vformat(R"(No constructor of "%s" matches the signature "%s".)", Variant::get_type_name(builtin_type), signature), p_call);
				}
			}

			if (builtin_type == Variant::CALLABLE && p_call->arguments.size() == 2) {
				FSParser::DataType callable_type;
				if (call_site_validation.callable_type_from_constant_method_args(p_call, 0, 1, callable_type)) {
					call_type = callable_type;
				} else {
					call_site_validation.validate_strict_callable_method_fallback(p_call, p_call->arguments[0]->get_datatype(), 1);
				}
			}
			if (builtin_type == Variant::SIGNAL && p_call->arguments.size() == 2) {
				FSParser::DataType signal_type;
				if (call_site_validation.signal_type_from_receiver(p_call->arguments[0]->get_datatype(), p_call, 1, signal_type)) {
					call_type = signal_type;
				} else {
					call_site_validation.validate_strict_signal_name_fallback(p_call, p_call->arguments[0]->get_datatype(), 1);
				}
			}

#ifdef DEBUG_ENABLED
			// Consider `Signal(self, "my_signal")` as an implicit use of the signal.
			if (builtin_type == Variant::SIGNAL && p_call->arguments.size() >= 2) {
				const FSParser::ExpressionNode *object_arg = p_call->arguments[0];
				if (object_arg && object_arg->type == FSParser::Node::SELF) {
					const FSParser::ExpressionNode *signal_arg = p_call->arguments[1];
					if (signal_arg && signal_arg->is_constant) {
						const StringName &signal_name = signal_arg->reduced_value;
						if (parser->current_class->has_member(signal_name)) {
							const FSParser::ClassNode::Member &member = parser->current_class->get_member(signal_name);
							if (member.type == FSParser::ClassNode::Member::SIGNAL) {
								member.signal->usages++;
							}
						}
					}
				}
			}
#endif // DEBUG_ENABLED

			p_call->set_datatype(call_type);
#ifdef DEBUG_ENABLED
			warn_return_value_discarded();
#endif // DEBUG_ENABLED
			return;
		} else if (FSUtilityFunctions::function_exists(function_name)) {
			MethodInfo function_info = FSUtilityFunctions::get_function_info(function_name);

			if (!p_is_root && !p_is_await && function_info.return_val.type == Variant::NIL && ((function_info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0)) {
				push_error(vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", function_name), p_call);
			}

			if (all_is_constant && FSUtilityFunctions::is_function_constant(function_name)) {
				// Can call on compilation.
				Vector<const Variant *> args;
				for (int i = 0; i < p_call->arguments.size(); i++) {
					args.push_back(&(p_call->arguments[i]->reduced_value));
				}

				Variant value;
				Callable::CallError err;
				FSUtilityFunctions::get_function(function_name)(&value, (const Variant **)args.ptr(), args.size(), err);

				switch (err.error) {
					case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
						if (value.get_type() == Variant::STRING && !value.operator String().is_empty()) {
							push_error(vformat(R"*(Invalid argument for "%s()" function: %s)*", function_name, value), p_call->arguments[err.argument]);
						} else {
							// Do not use `type_from_property()` for expected type, since utility functions use their own checks.
							push_error(vformat(R"*(Invalid argument for "%s()" function: argument %d should be "%s" but is "%s".)*", function_name, err.argument + 1,
											   Variant::get_type_name((Variant::Type)err.expected), p_call->arguments[err.argument]->get_datatype().to_string()),
									p_call->arguments[err.argument]);
						}
						break;
					case Callable::CallError::CALL_ERROR_INVALID_METHOD:
						push_error(vformat(R"(Invalid call for function "%s".)", function_name), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
						push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
						push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_METHOD_NOT_CONST:
					case Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL:
						break; // Can't happen in a builtin constructor.
					case Callable::CallError::CALL_OK:
						p_call->is_constant = true;
						p_call->reduced_value = value;
						break;
				}
			} else {
				call_site_validation.validate_call_arg(function_info, p_call);
			}
			p_call->set_datatype(type_from_property(function_info.return_val));
#ifdef DEBUG_ENABLED
			warn_return_value_discarded();
#endif // DEBUG_ENABLED
			return;
		} else if (Variant::has_utility_function(function_name)) {
			MethodInfo function_info = info_from_utility_func(function_name);

			if (!p_is_root && !p_is_await && function_info.return_val.type == Variant::NIL && ((function_info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0)) {
				push_error(vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", function_name), p_call);
			}

			if (all_is_constant && Variant::get_utility_function_type(function_name) == Variant::UTILITY_FUNC_TYPE_MATH) {
				// Can call on compilation.
				Vector<const Variant *> args;
				for (int i = 0; i < p_call->arguments.size(); i++) {
					args.push_back(&(p_call->arguments[i]->reduced_value));
				}

				Variant value;
				Callable::CallError err;
				Variant::call_utility_function(function_name, &value, (const Variant **)args.ptr(), args.size(), err);

				switch (err.error) {
					case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
						if (value.get_type() == Variant::STRING && !value.operator String().is_empty()) {
							push_error(vformat(R"*(Invalid argument for "%s()" function: %s)*", function_name, value), p_call->arguments[err.argument]);
						} else {
							// Do not use `type_from_property()` for expected type, since utility functions use their own checks.
							push_error(vformat(R"*(Invalid argument for "%s()" function: argument %d should be "%s" but is "%s".)*", function_name, err.argument + 1,
											   Variant::get_type_name((Variant::Type)err.expected), p_call->arguments[err.argument]->get_datatype().to_string()),
									p_call->arguments[err.argument]);
						}
						break;
					case Callable::CallError::CALL_ERROR_INVALID_METHOD:
						push_error(vformat(R"(Invalid call for function "%s".)", function_name), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
						push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
						push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_METHOD_NOT_CONST:
					case Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL:
						break; // Can't happen in a builtin constructor.
					case Callable::CallError::CALL_OK:
						p_call->is_constant = true;
						p_call->reduced_value = value;
						break;
				}
			} else {
				call_site_validation.validate_call_arg(function_info, p_call);
			}
			p_call->is_noreturn = function_name == SNAME("push_fatal");
			p_call->set_datatype(type_from_property(function_info.return_val));
#ifdef DEBUG_ENABLED
			warn_return_value_discarded();
#endif // DEBUG_ENABLED
			return;
		}
	}

	FSParser::DataType base_type;
	call_type.kind = FSParser::DataType::VARIANT;
	bool is_self = false;

	if (p_call->is_super) {
		base_type = parser->current_class->base_type;
		base_type.is_meta_type = false;
		is_self = true;

		if (p_call->callee == nullptr && current_lambda != nullptr) {
			push_error("Cannot use `super()` inside a lambda.", p_call);
		}
	} else if (callee_type == FSParser::Node::IDENTIFIER) {
		base_type = parser->current_class->get_datatype();
		base_type.is_meta_type = false;
		is_self = true;
	} else if (callee_type == FSParser::Node::SUBSCRIPT) {
		FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(p_call->callee);
		if (subscript->base == nullptr) {
			// Invalid syntax, error already set on parser.
			p_call->set_datatype(call_type);
			mark_node_unsafe(p_call);
			return;
		}
		if (!subscript->is_attribute) {
			// A `name[...](...)` call. When `name` is a generic method in scope, the brackets are
			// an explicit type-argument list and the call is dispatched on `self`. Otherwise this
			// is a genuine call on an index expression and stays an error.
			FSParser::FunctionNode *generic_method = nullptr;
			bool is_proxy_builtin = false;
			if (subscript->base->type == FSParser::Node::IDENTIFIER) {
				FSParser::IdentifierNode *base_identifier = static_cast<FSParser::IdentifierNode *>(subscript->base);
				const StringName &base_name = base_identifier->name;
				// A local variable or parameter named like the method shadows it, so `name[...]()` is
				// an index call, not a generic application. The parser records the binding the name
				// resolved to at this position, so this stays correctly scoped (a local declared later
				// in the block does not shadow an earlier call).
				bool shadowed_by_local = false;
				switch (base_identifier->source) {
					case FSParser::IdentifierNode::LOCAL_VARIABLE:
					case FSParser::IdentifierNode::LOCAL_CONSTANT:
					case FSParser::IdentifierNode::FUNCTION_PARAMETER:
					case FSParser::IdentifierNode::LOCAL_ITERATOR:
					case FSParser::IdentifierNode::LOCAL_BIND:
						shadowed_by_local = true;
						break;
					default:
						break;
				}
				bool resolved_to_member = false;
				if (!shadowed_by_local) {
					generic_method = find_generic_method(parser->current_class, base_name, resolved_to_member);
				}
				// `create_proxy[T](handler)` is the built-in generic proxy constructor when it is not
				// shadowed by a local or a user-declared member of the same name.
				if (!shadowed_by_local && !resolved_to_member && generic_method == nullptr && base_name == SNAME("create_proxy")) {
					is_proxy_builtin = true;
				}
			}

			// A `receiver.method[...](...)` call applies an explicit type-argument list to a generic
			// method resolved on the receiver rather than self-dispatching. Only a generic method
			// (which only user FoundryScript classes declare) accepts the bracket list on an instance
			// receiver; a non-generic method keeps erroring like an index call.
			bool dispatched_on_receiver = false;
			if (!is_proxy_builtin && generic_method == nullptr && subscript->base->type == FSParser::Node::SUBSCRIPT) {
				FSParser::SubscriptNode *receiver_access = static_cast<FSParser::SubscriptNode *>(subscript->base);
				if (receiver_access->is_attribute && receiver_access->attribute != nullptr && receiver_access->base != nullptr) {
					reduce_expression(receiver_access->base);
					FSParser::DataType receiver_type = receiver_access->base->get_datatype();
					const StringName &method_name = receiver_access->attribute->name;
					bool receiver_found_member = false;
					generic_method = find_generic_method(receiver_type.class_type, method_name, receiver_found_member);
					if (generic_method != nullptr) {
						base_type = receiver_type;
						is_self = receiver_access->base->type == FSParser::Node::SELF;
						if (p_call->function_name == StringName()) {
							p_call->function_name = method_name;
						}
						dispatched_on_receiver = true;
					}
				}
			}

			if (is_proxy_builtin) {
				reduce_call_create_proxy(p_call, subscript);
				return;
			}

			if (generic_method == nullptr) {
				push_error(R"*(Cannot call on an expression. Use ".call()" if it's a Callable.)*", p_call);
				p_call->set_datatype(call_type);
				mark_node_unsafe(p_call);
				return;
			}

			if (!dispatched_on_receiver) {
				base_type = parser->current_class->get_datatype();
				base_type.is_meta_type = false;
				is_self = true;
			}
		} else {
			if (subscript->attribute == nullptr) {
				// Invalid call. Error already sent in parser.
				p_call->set_datatype(call_type);
				mark_node_unsafe(p_call);
				return;
			}

			FSParser::IdentifierNode *base_id = nullptr;
			if (subscript->base->type == FSParser::Node::IDENTIFIER) {
				base_id = static_cast<FSParser::IdentifierNode *>(subscript->base);
			}
			if (base_id && FSParser::get_builtin_type(base_id->name) < Variant::VARIANT_MAX) {
				base_type = make_builtin_meta_type(FSParser::get_builtin_type(base_id->name));
			} else {
				reduce_expression(subscript->base);
				base_type = subscript->base->get_datatype();
				is_self = subscript->base->type == FSParser::Node::SELF;
			}
		}
	} else {
		// Invalid call. Error already sent in parser.
		// TODO: Could check if Callable here too.
		p_call->set_datatype(call_type);
		mark_node_unsafe(p_call);
		return;
	}

	// A named tuple declaration is callable as its own constructor, e.g. `Vec2(1.0, 2.0)`.
	{
		// A local variable, constant, parameter, or bind of the same name shadows the declaration, so
		// the call stays an ordinary (callable) invocation on that value.
		bool shadowed_by_local = false;
		if (is_self && p_call->callee != nullptr && p_call->callee->type == FSParser::Node::IDENTIFIER) {
			switch (static_cast<const FSParser::IdentifierNode *>(p_call->callee)->source) {
				case FSParser::IdentifierNode::LOCAL_VARIABLE:
				case FSParser::IdentifierNode::LOCAL_CONSTANT:
				case FSParser::IdentifierNode::FUNCTION_PARAMETER:
				case FSParser::IdentifierNode::LOCAL_ITERATOR:
				case FSParser::IdentifierNode::LOCAL_BIND:
					shadowed_by_local = true;
					break;
				default:
					break;
			}
		}

		FSParser::DataType tuple_meta_type;
		if (!shadowed_by_local && find_named_tuple_meta_type(base_type, is_self, p_call->function_name, p_call, tuple_meta_type)) {
			reduce_call_tuple_construction(p_call, tuple_meta_type);
			return;
		}
		if (!shadowed_by_local && is_self && find_global_tuple_meta_type(p_call->function_name, p_call, tuple_meta_type)) {
			reduce_call_tuple_construction(p_call, tuple_meta_type);
			return;
		}
	}

	// A payload-carrying case of a tagged union is callable as its own constructor, e.g. `Message.Move(1, 2)`.
	if (base_type.is_set() && base_type.kind == FSParser::DataType::ENUM && base_type.is_meta_type &&
			base_type.is_tagged_union && base_type.get_enum_case_payload(p_call->function_name) != nullptr) {
		reduce_call_enum_case_construction(p_call, base_type);
		return;
	}

	// Tuples are immutable and expose no methods; the underlying Array is an erasure detail.
	if (base_type.is_set() && base_type.kind == FSParser::DataType::TUPLE) {
		push_error(vformat(R"*(Cannot call "%s()" on tuple "%s"; tuples are immutable and expose no methods.)*",
						   p_call->function_name, base_type.to_string()),
				p_call);
		p_call->set_datatype(call_type);
		return;
	}

	int default_arg_count = 0;
	BitField<MethodFlags> method_flags = {};
	FSParser::DataType return_type;
	List<FSParser::DataType> par_types;
	bool is_noreturn = false;

	bool is_constructor = (base_type.is_meta_type || base_type.is_type_handle_annotation || (p_call->callee && p_call->callee->type == FSParser::Node::IDENTIFIER)) && p_call->function_name == SNAME("new");

	if (is_constructor) {
		if (base_type.kind == FSParser::DataType::CLASS && base_type.class_type != nullptr && base_type.class_type->is_trait) {
			push_error(vformat(R"(Cannot construct trait "%s".)", _class_or_trait_name(base_type.class_type)), p_call);
			call_type.kind = FSParser::DataType::VARIANT;
			call_type.type_source = FSParser::DataType::INFERRED;
			p_call->set_datatype(call_type);
			return;
		}
		if (Engine::get_singleton()->has_singleton(base_type.native_type)) {
			push_error(vformat(R"(Cannot construct native class "%s" because it is an engine singleton.)", base_type.native_type), p_call);
			p_call->set_datatype(call_type);
			return;
		}
		if ((base_type.kind == FSParser::DataType::CLASS && base_type.class_type->is_abstract) || (base_type.kind == FSParser::DataType::SCRIPT && base_type.script_type.is_valid() && base_type.script_type->is_abstract())) {
			push_error(vformat(R"(Cannot construct abstract class "%s".)", base_type.to_string()), p_call);
		}
	}

	FSParser::FunctionNode *found_function = nullptr;
	if (get_function_signature(p_call, is_constructor, base_type, p_call->function_name, return_type, par_types,
				default_arg_count, method_flags, nullptr, &is_noreturn, &found_function)) {
		p_call->is_static = method_flags.has_flag(METHOD_FLAG_STATIC);
		p_call->is_noreturn = is_noreturn;

		const bool is_enum_function_call = found_function != nullptr && found_function->owner_enum != nullptr;
		if (is_enum_function_call) {
			const bool receiver_matches = found_function->is_static == base_type.is_meta_type;
			if (!receiver_matches) {
				const char *function_kind = found_function->is_static ? "static" : "instance";
				const char *receiver_kind = base_type.is_meta_type ? "type" : "value";
				push_error(vformat(R"*(Cannot call %s enum function "%s()" on enum %s "%s".)*",
								   function_kind, p_call->function_name, receiver_kind, base_type.enum_type),
						p_call->callee);
			} else {
				const FSParser::DataType &enum_type = found_function->owner_enum->get_datatype();
				p_call->enum_call_kind = found_function->is_static ? FSParser::CallNode::ENUM_CALL_STATIC : FSParser::CallNode::ENUM_CALL_INSTANCE;
				p_call->enum_call_owner_script_path = enum_type.script_path;
				p_call->enum_call_owner_class = enum_type.class_type != nullptr ? StringName(enum_type.class_type->fqcn) : StringName();
				p_call->enum_call_enum_type = enum_type.enum_type;
				p_call->enum_call_function = p_call->function_name;
			}
		}

		const FSParser::FunctionNode *enum_function = parser->current_function;
		while (enum_function != nullptr && enum_function->owner_enum == nullptr && enum_function->source_lambda != nullptr) {
			enum_function = enum_function->source_lambda->parent_function;
		}
		const bool enum_outer_instance_call = is_self && found_function != nullptr &&
				found_function->owner_enum == nullptr && !found_function->is_static &&
				enum_function != nullptr && enum_function->owner_enum != nullptr;
		if (enum_outer_instance_call) {
			push_error(vformat(
							   R"*(Enum function "%s()" cannot access containing class instance member "%s".)*",
							   enum_function->identifier->name, p_call->function_name),
					p_call->callee);
		}

		// Named arguments are only valid against a statically resolved FoundryScript function. When the
		// callee resolves to one, rewrite `name = value` arguments into canonical positional order
		// so the existing positional validation and codegen run unchanged; otherwise reject them.
		bool named_arguments_valid = true;
		if (found_function != nullptr) {
			named_arguments_valid = call_site_validation.canonicalize_named_call_arguments(p_call, found_function);
			// Reordering may have changed argument positions, so rebuild the literal-typing maps.
			arrays.clear();
			dictionaries.clear();
			for (int i = 0; i < p_call->arguments.size(); i++) {
				if (p_call->arguments[i]->type == FSParser::Node::ARRAY) {
					arrays[i] = static_cast<FSParser::ArrayNode *>(p_call->arguments[i]);
				} else if (p_call->arguments[i]->type == FSParser::Node::DICTIONARY) {
					dictionaries[i] = static_cast<FSParser::DictionaryNode *>(p_call->arguments[i]);
				}
			}
		} else {
			call_site_validation.reject_named_call_arguments(p_call);
		}

		// Generic methods solve their type parameters here, substituting the call's parameter
		// and return types before the arguments are validated against them.
		if (!is_constructor && found_function != nullptr && !found_function->type_parameters.is_empty()) {
			call_site_validation.apply_generic_method_call(p_call, found_function, par_types, return_type);
		}
		// If the method is implemented in the class hierarchy, the virtual/abstract flag will not be set for that `MethodInfo` and the search stops there.
		// Virtual/abstract check only possible for super calls because class hierarchy is known. Objects may have scripts attached we don't know of at compile-time.
		if (p_call->is_super) {
			if (method_flags.has_flag(METHOD_FLAG_VIRTUAL)) {
				push_error(vformat(R"*(Cannot call the parent class' virtual function "%s()" because it hasn't been defined.)*", p_call->function_name), p_call);
			} else if (method_flags.has_flag(METHOD_FLAG_VIRTUAL_REQUIRED)) {
				push_error(vformat(R"*(Cannot call the parent class' abstract function "%s()" because it hasn't been defined.)*", p_call->function_name), p_call);
			}
		}

		// If the function requires typed arrays we must make literals be typed.
		for (const KeyValue<int, FSParser::ArrayNode *> &E : arrays) {
			int index = E.key;
			if (index < par_types.size() && par_types.get(index).is_hard_type() && par_types.get(index).has_container_element_type(0)) {
				const FSParser::DataType par_type = par_types.get(index);
				update_array_literal_element_type(E.value,
						par_type.get_container_element_type(0),
						_datatype_contains_self_type_parameter(par_type));
			}
		}
		for (const KeyValue<int, FSParser::DictionaryNode *> &E : dictionaries) {
			int index = E.key;
			if (index < par_types.size() && par_types.get(index).is_hard_type() && par_types.get(index).has_container_element_types()) {
				const FSParser::DataType par_type = par_types.get(index);
				FSParser::DataType key = par_type.get_container_element_type_or_variant(0);
				FSParser::DataType value = par_type.get_container_element_type_or_variant(1);
				update_dictionary_literal_element_type(E.value, key, value, _datatype_contains_self_type_parameter(par_type));
			}
		}
		p_call->resolved_parameter_types.clear();
		for (const FSParser::DataType &par_type : par_types) {
			p_call->resolved_parameter_types.push_back(
					_datatype_contains_self_type_parameter(par_type) ? _substitute_self_type_parameter_with_bounds(par_type) : par_type);
		}

		if (named_arguments_valid) {
			call_site_validation.validate_call_arg(par_types, default_arg_count, method_flags.has_flag(METHOD_FLAG_VARARG), p_call, base_type.method_extra_allowed_argument_counts, base_type.method_unbound_argument_count);
		}
		call_site_validation.validate_signal_connect_arg(base_type, p_call);
		call_site_validation.validate_local_object_signal_callable_arg(p_call, is_self);
		call_site_validation.validate_local_object_emit_signal_args(p_call, is_self);
		call_site_validation.validate_typed_object_signal_api_args(base_type, p_call, is_self);

		if (base_type.kind == FSParser::DataType::ENUM && base_type.is_meta_type) {
			// Enum type is treated as a dictionary value for function calls.
			base_type.is_meta_type = false;
		}

		if (is_self && static_context && !p_call->is_static && !enum_outer_instance_call) {
			// Get the parent function above any lambda.
			FSParser::FunctionNode *parent_function = parser->current_function;
			while (parent_function && parent_function->source_lambda) {
				parent_function = parent_function->source_lambda->parent_function;
			}

			if (parent_function) {
				push_error(vformat(R"*(Cannot call non-static function "%s()" from the static function "%s()".)*", p_call->function_name, parent_function->identifier->name), p_call);
			} else {
				push_error(vformat(R"*(Cannot call non-static function "%s()" from a static variable initializer.)*", p_call->function_name), p_call);
			}
		} else if (!is_self && base_type.is_meta_type && !p_call->is_static) {
			base_type.is_meta_type = false; // For `to_string()`.
			if (base_type.is_type_handle_annotation) {
				push_error(vformat(R"*(Cannot call non-static function "%s()" on class handle "%s" directly. )*"
								   R"*(Make an instance instead.)*",
								   p_call->function_name, base_type.to_string()),
						p_call);
			} else {
				push_error(vformat(R"*(Cannot call non-static function "%s()" on the class "%s" directly. )*"
								   R"*(Make an instance instead.)*",
								   p_call->function_name, base_type.to_string()),
						p_call);
			}
		} else if (is_self && !p_call->is_static) {
			mark_lambda_use_self();
		}

		if (!p_is_root && !p_is_await && return_type.is_hard_type() && return_type.kind == FSParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL) {
			push_error(vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", p_call->function_name), p_call);
		}

#ifdef DEBUG_ENABLED
		if (p_is_root && return_type.kind != FSParser::DataType::UNRESOLVED && return_type.builtin_type != Variant::NIL && !return_type.is_coroutine &&
				!(p_call->is_super && p_call->function_name == FSLanguage::get_singleton()->strings._init)) {
			parser->push_warning(p_call, FSWarning::RETURN_VALUE_DISCARDED, p_call->function_name);
		}

		if (method_flags.has_flag(METHOD_FLAG_STATIC) && !is_constructor && !base_type.is_meta_type && !is_self && !is_enum_function_call) {
			String caller_type = base_type.to_string();

			parser->push_warning(p_call, FSWarning::STATIC_CALLED_ON_INSTANCE, p_call->function_name, caller_type);
		}

		// Consider `emit_signal()`, `connect()`, `disconnect()`, and `is_connected()` as implicit uses of the signal.
		if (is_self && (p_call->function_name == SNAME("emit_signal") || p_call->function_name == SNAME("connect") || p_call->function_name == SNAME("disconnect") || p_call->function_name == SNAME("is_connected")) && !p_call->arguments.is_empty()) {
			const FSParser::ExpressionNode *signal_arg = p_call->arguments[0];
			if (signal_arg && signal_arg->is_constant) {
				const StringName &signal_name = signal_arg->reduced_value;
				if (parser->current_class->has_member(signal_name)) {
					const FSParser::ClassNode::Member &member = parser->current_class->get_member(signal_name);
					if (member.type == FSParser::ClassNode::Member::SIGNAL) {
						member.signal->usages++;
					}
				}
			}
		}
#endif // DEBUG_ENABLED

		if (is_constructor && base_type.is_type_handle_annotation) {
			return_type = type_handle_represented_type(base_type);
		}

		// Constructing a specialized generic class (`Box[int].new()`) yields a specialized instance,
		// so the call's result carries the reified type arguments supplied at the base.
		if (is_constructor && base_type.has_type_arguments()) {
			return_type.type_arguments = base_type.type_arguments;
		}

		call_type = return_type;
	} else {
		// The callee could not be resolved to a statically known FoundryScript signature (dynamic or
		// `Variant` receiver, or an unresolved method on a typed base). Named arguments are
		// canonicalized entirely at compile time against such a signature, so without one their
		// names cannot be mapped to positions. Reject them here instead of letting codegen drop
		// the names and silently pass the values positionally in source order.
		call_site_validation.reject_named_call_arguments(p_call);

		bool found = false;

		if (base_type.kind == FSParser::DataType::ENUM) {
			if (!base_type.is_meta_type) {
				push_error(vformat(R"*(Function "%s()" does not exist for enum value "%s".)*",
								   p_call->function_name, base_type.enum_type),
						p_call->callee);
			} else if (base_type.builtin_type == Variant::DICTIONARY) {
				push_error(vformat(R"*(Function "%s()" does not exist for enum "%s" or its Dictionary methods.)*",
								   p_call->function_name, base_type.enum_type),
						p_call->callee);
			} else {
				push_error(vformat(R"*(The native enum "%s" does not behave like Dictionary and does not have methods of its own.)*", base_type.enum_type), p_call->callee);
			}
			found = true;
		} else if (!p_call->is_super && callee_type != FSParser::Node::NONE) { // Check if the name exists as something else.
			FSParser::IdentifierNode *callee_id;
			if (callee_type == FSParser::Node::IDENTIFIER) {
				callee_id = static_cast<FSParser::IdentifierNode *>(p_call->callee);
			} else {
				// Can only be attribute.
				callee_id = static_cast<FSParser::SubscriptNode *>(p_call->callee)->attribute;
			}
			if (callee_id) {
				reduce_identifier_from_base(callee_id, &base_type);
				FSParser::DataType callee_datatype = callee_id->get_datatype();
				if (callee_datatype.is_set() && !callee_datatype.is_variant()) {
					found = true;
					if (callee_datatype.builtin_type == Variant::CALLABLE) {
						push_error(vformat(R"*(Name "%s" is a Callable. You can call it with "%s.call()" instead.)*", p_call->function_name, p_call->function_name), p_call->callee);
					} else {
						push_error(vformat(R"*(Name "%s" called as a function but is a "%s".)*", p_call->function_name, callee_datatype.to_string()), p_call->callee);
					}
				} else if (!is_self && !(base_type.is_hard_type() && base_type.kind == FSParser::DataType::BUILTIN)) {
					if (strict_dynamic_checks) {
						push_error(vformat(R"*(Cannot resolve method "%s" on type "%s" in strict dynamic mode.)*", p_call->function_name, base_type.to_string()), p_call->callee);
					} else {
#ifdef DEBUG_ENABLED
						parser->push_warning(p_call, FSWarning::UNSAFE_METHOD_ACCESS, p_call->function_name, base_type.to_string());
						mark_node_unsafe(p_call);
#endif // DEBUG_ENABLED
					}
				}
			}
		}
		if (!found && (is_self || (base_type.is_hard_type() && base_type.kind == FSParser::DataType::BUILTIN))) {
			String base_name = is_self && !p_call->is_super ? "self" : base_type.to_string();
			push_error(vformat(R"*(Function "%s()" not found in base %s.)*", p_call->function_name, base_name), p_call->is_super ? p_call : p_call->callee);
		} else if (!found && (!p_call->is_super && base_type.is_hard_type() && base_type.is_meta_type)) {
			push_error(vformat(R"*(Static function "%s()" not found in base "%s".)*", p_call->function_name, base_type.to_string()), p_call);
		}
	}

	if (call_type.is_coroutine && !p_is_await && p_is_root) {
		// Honest Coroutine[T] typing makes a held or passed coroutine well-typed, so only a discarded
		// coroutine *statement* (root position) still warns about a probably-forgotten "await".
#ifdef DEBUG_ENABLED
		parser->push_warning(p_call, FSWarning::MISSING_AWAIT);
#endif // DEBUG_ENABLED
	}

	p_call->set_datatype(call_type);
}

void FSAnalyzer::reduce_cast(FSParser::CastNode *p_cast) {
	reduce_expression(p_cast->operand);

	FSParser::DataType cast_type = type_from_metatype(resolve_datatype(p_cast->cast_type));

	if (!cast_type.is_set()) {
		mark_node_unsafe(p_cast);
		return;
	}

	p_cast->set_datatype(cast_type);
	if (p_cast->operand->is_constant) {
		update_const_expression_builtin_type(p_cast->operand, cast_type, "cast", true);
		if (cast_type.is_variant() || p_cast->operand->get_datatype() == cast_type) {
			p_cast->is_constant = true;
			p_cast->reduced_value = p_cast->operand->reduced_value;
		}
	}

	if (p_cast->operand->type == FSParser::Node::ARRAY && cast_type.has_container_element_type(0)) {
		update_array_literal_element_type(static_cast<FSParser::ArrayNode *>(p_cast->operand), cast_type.get_container_element_type(0));
	}

	if (p_cast->operand->type == FSParser::Node::DICTIONARY && cast_type.has_container_element_types()) {
		update_dictionary_literal_element_type(static_cast<FSParser::DictionaryNode *>(p_cast->operand),
				cast_type.get_container_element_type_or_variant(0), cast_type.get_container_element_type_or_variant(1));
	}

	if (!cast_type.is_variant()) {
		FSParser::DataType op_type = p_cast->operand->get_datatype();
		if (op_type.is_variant() || !op_type.is_hard_type()) {
			mark_node_unsafe(p_cast);
#ifdef DEBUG_ENABLED
			parser->push_warning(p_cast, FSWarning::UNSAFE_CAST, cast_type.to_string());
#endif // DEBUG_ENABLED
		} else {
			bool valid = false;
			// A tagged union is not int-backed, so neither direction of the int/enum cast applies to it.
			if (op_type.builtin_type == Variant::INT && cast_type.kind == FSParser::DataType::ENUM && !cast_type.is_tagged_union) {
				mark_node_unsafe(p_cast);
				valid = true;
			} else if (op_type.kind == FSParser::DataType::ENUM && !op_type.is_tagged_union && cast_type.builtin_type == Variant::INT) {
				valid = true;
			} else if (op_type.kind == FSParser::DataType::BUILTIN && cast_type.kind == FSParser::DataType::BUILTIN) {
				valid = Variant::can_convert(op_type.builtin_type, cast_type.builtin_type);
			} else if (op_type.kind != FSParser::DataType::BUILTIN && cast_type.kind != FSParser::DataType::BUILTIN) {
				valid = is_type_compatible(cast_type, op_type) || is_type_compatible(op_type, cast_type);
			}

			if (!valid) {
				const bool operand_is_union_to_int = op_type.is_tagged_union_type() && cast_type.builtin_type == Variant::INT;
				const bool int_to_union = cast_type.is_tagged_union_type() && op_type.builtin_type == Variant::INT;
				if (operand_is_union_to_int || int_to_union) {
					push_error(vformat(R"(Tagged union "%s" is not int-backed, because its cases carry payloads; it cannot be converted to or from "int".)",
									   operand_is_union_to_int ? op_type.enum_type : cast_type.enum_type),
							p_cast->cast_type);
				} else {
					push_error(vformat(R"(Invalid cast. Cannot convert from "%s" to "%s".)", op_type.to_string(), cast_type.to_string()), p_cast->cast_type);
				}
			}
		}
	}
}

void FSAnalyzer::reduce_dictionary(FSParser::DictionaryNode *p_dictionary) {
	HashMap<Variant, FSParser::ExpressionNode *, HashMapHasherDefault, StringLikeVariantComparator> elements;

	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		const FSParser::DictionaryNode::Pair &element = p_dictionary->elements[i];
		if (p_dictionary->style == FSParser::DictionaryNode::PYTHON_DICT) {
			reduce_expression(element.key);
		}
		reduce_expression(element.value);

		if (element.key->is_constant) {
			if (elements.has(element.key->reduced_value)) {
				push_error(vformat(R"(Key "%s" was already used in this dictionary (at line %d).)", element.key->reduced_value, elements[element.key->reduced_value]->start_line), element.key);
			} else {
				elements[element.key->reduced_value] = element.value;
			}
		}
	}

	// It's dictionary in any case.
	FSParser::DataType dict_type;
	dict_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	dict_type.kind = FSParser::DataType::BUILTIN;
	dict_type.builtin_type = Variant::DICTIONARY;
	dict_type.is_constant = true;

	p_dictionary->set_datatype(dict_type);
}

void FSAnalyzer::reduce_get_node(FSParser::GetNodeNode *p_get_node) {
	FSParser::DataType result;
	result.kind = FSParser::DataType::VARIANT;

	if (!ClassDB::is_parent_class(parser->current_class->base_type.native_type, SNAME("Node"))) {
		push_error(vformat(R"*(Cannot use shorthand "get_node()" notation ("%c") on a class that isn't a node.)*", p_get_node->use_dollar ? '$' : '%'), p_get_node);
		p_get_node->set_datatype(result);
		return;
	}

	if (static_context) {
		push_error(vformat(R"*(Cannot use shorthand "get_node()" notation ("%c") in a static function.)*", p_get_node->use_dollar ? '$' : '%'), p_get_node);
		p_get_node->set_datatype(result);
		return;
	}

	mark_lambda_use_self();

	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	result.kind = FSParser::DataType::NATIVE;
	result.builtin_type = Variant::OBJECT;
	result.native_type = SNAME("Node");
	p_get_node->set_datatype(result);
}

bool FSAnalyzer::is_bootstrap_dependency_path_allowed(const String &p_path) const {
	if (bootstrap_allowed_dependency_root.is_empty()) {
		return true;
	}

	return _bootstrap_path_is_within_root(p_path, bootstrap_allowed_dependency_root);
}

bool FSAnalyzer::validate_bootstrap_namespace_import(
		const String &p_import, const LocalVector<StringName> &p_global_classes) {
	if (bootstrap_allowed_dependency_root.is_empty()) {
		return true;
	}

	const String namespace_prefix = p_import + ".";
	bool found_namespace_member = false;
	for (const StringName &global_class : p_global_classes) {
		if (!String(global_class).begins_with(namespace_prefix)) {
			continue;
		}

		found_namespace_member = true;
		const String path = ScriptServer::get_global_class_path(global_class);
		if (!_bootstrap_path_is_within_root(path, bootstrap_allowed_dependency_root)) {
			push_error(vformat(R"(Build task bootstrap cannot import namespace "%s"; global class "%s" from "%s" is outside the provider bootstrap root "%s".)",
							   p_import, global_class, path, bootstrap_allowed_dependency_root),
					parser->head);
			return false;
		}
	}

	FSLanguage *language = FSLanguage::get_singleton();
	if (language != nullptr) {
		List<StringName> annotations;
		language->get_global_annotation_list(&annotations);
		for (const StringName &annotation : annotations) {
			if (!String(annotation).begins_with(namespace_prefix)) {
				continue;
			}

			found_namespace_member = true;
			const String path = language->get_global_annotation_path(annotation);
			if (!_bootstrap_path_is_within_root(path, bootstrap_allowed_dependency_root)) {
				push_error(vformat(R"(Build task bootstrap cannot import namespace "%s"; annotation "%s" from "%s" is outside the provider bootstrap root "%s".)",
								   p_import, annotation, path, bootstrap_allowed_dependency_root),
						parser->head);
				return false;
			}
		}
	}

	if (!found_namespace_member) {
		push_error(vformat(R"(Could not find imported namespace "%s".)", p_import), parser->head);
		return false;
	}

	return true;
}

bool FSAnalyzer::reject_bootstrap_global_class_dependency(
		const StringName &p_class_name, const FSParser::Node *p_source, const String &p_context) {
	if (bootstrap_allowed_dependency_root.is_empty() || !ScriptServer::is_global_class(p_class_name)) {
		return false;
	}

	const String path = ScriptServer::get_global_class_path(p_class_name);
	if (FoundryScript::is_canonically_equal_paths(path, parser->script_path) ||
			_bootstrap_path_is_within_root(path, bootstrap_allowed_dependency_root)) {
		return false;
	}

	push_error(vformat(R"(Build task bootstrap cannot use %s "%s" from "%s"; it is outside the provider bootstrap root "%s".)",
					   p_context, p_class_name, path, bootstrap_allowed_dependency_root),
			p_source);
	return true;
}

void FSAnalyzer::set_bootstrap_allowed_dependency_root(const String &p_root) {
	bootstrap_allowed_dependency_root = _normalize_bootstrap_path(p_root);
	if (!bootstrap_allowed_dependency_root.is_empty() && !bootstrap_allowed_dependency_root.ends_with("/")) {
		bootstrap_allowed_dependency_root += "/";
	}
}

String FSAnalyzer::get_bootstrap_allowed_dependency_root() {
	return bootstrap_allowed_dependency_root;
}

FSParser::DataType FSAnalyzer::make_global_class_meta_type(const StringName &p_class_name, const FSParser::Node *p_source) {
	FSParser::DataType type;

	String path = ScriptServer::get_global_class_path(p_class_name);
	if (!bootstrap_allowed_dependency_root.is_empty() && !_bootstrap_path_is_within_root(path, bootstrap_allowed_dependency_root)) {
		push_error(vformat(R"(Build task bootstrap cannot use global class "%s" from "%s"; it is outside the provider bootstrap root "%s".)",
						   p_class_name, path, bootstrap_allowed_dependency_root),
				p_source);
		type.type_source = FSParser::DataType::UNDETECTED;
		type.kind = FSParser::DataType::VARIANT;
		return type;
	}

	String ext = path.get_extension();
	if (ext == FSLanguage::get_singleton()->get_extension()) {
		Ref<FSParserRef> ref;
		Error err = dependency_parser_access.raise_depended_parser_for(path, FSParserRef::INHERITANCE_SOLVED, ref);
		if (ref.is_null()) {
			push_error(vformat(R"(Could not find script for class "%s".)", p_class_name), p_source);
			type.type_source = FSParser::DataType::UNDETECTED;
			type.kind = FSParser::DataType::VARIANT;
			return type;
		}

		if (err) {
			push_error(vformat(R"(Could not resolve class "%s", because of a parser error.)", p_class_name), p_source);
			type.type_source = FSParser::DataType::UNDETECTED;
			type.kind = FSParser::DataType::VARIANT;
			return type;
		}

		FSParser::ClassNode *global_head = ref->get_parser()->head;
		if (global_head != nullptr && global_head->is_tuple_file) {
			// A `tuple_name` file has no script body: its global name denotes the tuple type itself,
			// resolved in the declaring file's own analyzer so its field types resolve in that scope.
			return ref->get_analyzer()->make_global_tuple_type_from_current_parser(p_class_name, global_head);
		}
		return global_head->get_datatype();
	} else {
		return make_script_meta_type(ResourceLoader::load(path, "Script"));
	}
}

FSParser::DataType FSAnalyzer::make_global_enum_type_from_current_parser(const StringName &p_global_name, const FSParser::Node *p_source) {
	FSParser::DataType error_type;
	error_type.type_source = FSParser::DataType::UNDETECTED;
	error_type.kind = FSParser::DataType::VARIANT;

	FSParser::ClassNode *head = parser->head;
	FSParser::EnumNode *enum_node = head != nullptr ? head->enum_file_decl : nullptr;
	if (head == nullptr || !head->is_enum_file || enum_node == nullptr || enum_node->identifier == nullptr) {
		push_error(vformat(R"(Global enum "%s" does not refer to an enum_name file.)", p_global_name), p_source);
		return error_type;
	}

	if (enum_node->get_datatype().is_resolving()) {
		push_error(vformat(R"(Could not resolve global enum "%s": Cyclic reference.)", p_global_name), p_source);
		return error_type;
	}
	if (enum_node->get_datatype().is_set()) {
		return enum_node->get_datatype();
	}

	FSParser::DataType resolving_datatype;
	resolving_datatype.kind = FSParser::DataType::RESOLVING;
	enum_node->set_datatype(resolving_datatype);

	FSParser::DataType enum_type = make_standalone_global_enum_type(p_global_name, true);
	enum_type.class_type = head;
	enum_type.script_path = parser->script_path;
	return resolve_enum_values(enum_node, enum_type, head);
}

FSParser::DataType FSAnalyzer::make_global_tuple_type_from_current_parser(const StringName &p_global_name, const FSParser::Node *p_source) {
	FSParser::DataType error_type;
	error_type.type_source = FSParser::DataType::UNDETECTED;
	error_type.kind = FSParser::DataType::VARIANT;

	FSParser::ClassNode *head = parser->head;
	FSParser::TupleNode *tuple_node = head != nullptr ? head->tuple_file_decl : nullptr;
	if (head == nullptr || !head->is_tuple_file || tuple_node == nullptr || tuple_node->identifier == nullptr) {
		push_error(vformat(R"(Global tuple "%s" does not refer to a "tuple_name" file.)", p_global_name), p_source);
		return error_type;
	}

	if (tuple_node->get_datatype().is_resolving()) {
		push_error(vformat(R"(Tuple "%s" cannot contain itself by value.)", p_global_name), p_source);
		return error_type;
	}
	if (tuple_node->get_datatype().is_set()) {
		return tuple_node->get_datatype();
	}

	// Marking the declaration as resolving turns a by-value cycle across files into an error at the
	// field that closes the loop, exactly like a class-body tuple declaration.
	FSParser::DataType resolving_datatype;
	resolving_datatype.kind = FSParser::DataType::RESOLVING;
	tuple_node->set_datatype(resolving_datatype);

	FSParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = head;
	Vector<FSParser::DataType> element_types;
	Vector<StringName> field_names;
	for (int i = 0; i < tuple_node->fields.size(); i++) {
		const FSParser::TupleNode::Field &field = tuple_node->fields[i];
		element_types.push_back(type_from_metatype(resolve_datatype(field.type)));
		field_names.push_back(field.identifier != nullptr ? field.identifier->name : StringName());
	}
	parser->current_class = previous_class;

	// The nominal identity of a whole-file tuple is its global name, so every script that references
	// it names the same type. It is computed from the declaration rather than from the reference so a
	// namespaced tuple keeps one identity however it is imported.
	const StringName global_name = head->qualified_global_name.is_empty()
			? tuple_node->identifier->name
			: StringName(head->qualified_global_name);
	FSParser::DataType tuple_type = make_tuple_type(global_name, String(), parser->script_path,
			element_types, field_names, true);
	tuple_node->set_datatype(tuple_type);
	return tuple_type;
}

FSParser::DataType FSAnalyzer::make_global_enum_type_from_path(const StringName &p_global_name, const String &p_path, const FSParser::Node *p_source) {
	FSParser::DataType error_type;
	error_type.type_source = FSParser::DataType::UNDETECTED;
	error_type.kind = FSParser::DataType::VARIANT;

	if (!bootstrap_allowed_dependency_root.is_empty() && !_bootstrap_path_is_within_root(p_path, bootstrap_allowed_dependency_root)) {
		push_error(vformat(R"(Build task bootstrap cannot use global enum "%s" from "%s"; it is outside the provider bootstrap root "%s".)",
						   p_global_name, p_path, bootstrap_allowed_dependency_root),
				p_source);
		return error_type;
	}

	if (FoundryScript::is_canonically_equal_paths(parser->script_path, p_path)) {
		return make_global_enum_type_from_current_parser(p_global_name, p_source);
	}

	Ref<FSParserRef> ref;
	Error err = dependency_parser_access.raise_depended_parser_for(p_path, FSParserRef::INHERITANCE_SOLVED, ref);
	if (ref.is_null()) {
		push_error(vformat(R"(Could not find script for enum "%s".)", p_global_name), p_source);
		return error_type;
	}

	if (err != OK) {
		push_error(vformat(R"(Could not resolve enum "%s", because of a parser error.)", p_global_name), p_source);
		return error_type;
	}

	FSParser *enum_parser = ref->get_parser();
	FSAnalyzer *enum_analyzer = ref->get_analyzer();
	const int error_count = enum_parser->errors.size();
	FSParser::DataType enum_type = enum_analyzer->make_global_enum_type_from_current_parser(p_global_name, enum_parser->head);
	if (enum_parser->errors.size() > error_count || !enum_type.is_set() || enum_type.kind != FSParser::DataType::ENUM) {
		push_error(vformat(R"(Could not resolve global enum "%s" from "%s".)", p_global_name, p_path), p_source);
		return error_type;
	}

	return enum_type;
}

bool FSAnalyzer::get_autoload_singleton_value_type(const StringName &p_name, FSParser::DataType &r_type) {
	if (FSLanguage::get_singleton()->is_reserved_global_name(p_name)) {
		return false;
	}

	ensure_autoload_index_current();
	const FSAutoloadIndexEntry *autoload = autoload_index.get_by_name(p_name);
	if (autoload == nullptr || !autoload->is_singleton) {
		return false;
	}
	if (!bootstrap_allowed_dependency_root.is_empty()) {
		return false;
	}

	FSParser::DataType result;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	result.kind = FSParser::DataType::NATIVE;
	result.builtin_type = Variant::OBJECT;
	result.native_type = SNAME("Node");

	String script_path = autoload->script_path;
	if (script_path.is_empty() && ResourceLoader::get_resource_type(autoload->path) == "PackedScene") {
		// Try to get script from scene if possible.
		if (FSLanguage::get_singleton()->has_any_global_constant(autoload->name)) {
			Variant constant = FSLanguage::get_singleton()->get_any_global_constant(autoload->name);
			Node *node = Object::cast_to<Node>(constant);
			if (node != nullptr) {
				Ref<FoundryScript> scr = node->get_script();
				if (scr.is_valid()) {
					script_path = scr->get_script_path();
				}
			}
		}
	}

	if (!script_path.is_empty()) {
		Ref<FSParserRef> single_parser = dependency_parser_access.depended_parser_for(script_path, FSParserRef::INHERITANCE_SOLVED);
		if (single_parser.is_valid()) {
			if (single_parser->get_status() >= FSParserRef::INHERITANCE_SOLVED) {
				result = type_from_metatype(single_parser->get_parser()->head->get_datatype());
			}
		}
	}

	result.is_constant = true;
	r_type = result;
	return true;
}

static String _join_identifier_chain(const Vector<FSParser::IdentifierNode *> &p_chain, int p_from, int p_count) {
	String result;
	for (int i = p_from; i < p_count; i++) {
		if (i > p_from) {
			result += ".";
		}
		result += String(p_chain[i]->name);
	}
	return result;
}

static bool _string_vector_has(const LocalVector<String> &p_strings, const String &p_string) {
	for (const String &string : p_strings) {
		if (string == p_string) {
			return true;
		}
	}
	return false;
}

static bool _namespace_exists_in_global_classes(const LocalVector<StringName> &p_global_classes, const String &p_namespace) {
	if (p_namespace.is_empty()) {
		return false;
	}

	const String namespace_prefix = p_namespace + ".";
	for (const StringName &global_class : p_global_classes) {
		if (String(global_class).begins_with(namespace_prefix)) {
			return true;
		}
	}

	return false;
}

#ifdef DEBUG_ENABLED
static String _get_namespace_warning_name(const String &p_namespace) {
	return p_namespace.is_empty() ? String("<global>") : p_namespace;
}

struct MixedNamespaceDirectoryClass {
	String script_path;
	String namespace_name;
};

struct MixedNamespaceDirectoryCache {
	uint64_t global_class_cache_version = uint64_t(-1);
	HashMap<String, Vector<MixedNamespaceDirectoryClass>> classes_by_directory;
};

static thread_local MixedNamespaceDirectoryCache mixed_namespace_directory_cache;

static String _format_namespace_warning_list(Vector<String> p_namespaces) {
	p_namespaces.sort();

	String result;
	for (int i = 0; i < p_namespaces.size(); i++) {
		if (i > 0) {
			result += i == p_namespaces.size() - 1 ? (p_namespaces.size() == 2 ? " and " : ", and ") : ", ";
		}
		result += "\"" + p_namespaces[i] + "\"";
	}
	return result;
}

static void _update_mixed_namespace_directory_cache() {
	const uint64_t global_class_cache_version = ScriptServer::get_global_class_cache_version();
	if (mixed_namespace_directory_cache.global_class_cache_version == global_class_cache_version) {
		return;
	}

	mixed_namespace_directory_cache.global_class_cache_version = global_class_cache_version;
	mixed_namespace_directory_cache.classes_by_directory.clear();

	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);

	for (const StringName &global_class : global_classes) {
		const String class_path = FoundryScript::canonicalize_path(ScriptServer::get_global_class_path(global_class));
		if (class_path.is_empty()) {
			continue;
		}

		const String class_dir = class_path.get_base_dir();
		if (class_dir.is_empty()) {
			continue;
		}

		String class_namespace;
		ScriptServer::get_global_class_name_parts(global_class, nullptr, &class_namespace);

		MixedNamespaceDirectoryClass class_info;
		class_info.script_path = class_path;
		class_info.namespace_name = _get_namespace_warning_name(class_namespace);
		mixed_namespace_directory_cache.classes_by_directory[class_dir].push_back(class_info);
	}
}
#endif // DEBUG_ENABLED

bool FSAnalyzer::get_global_class_in_namespace(const String &p_namespace, const StringName &p_class_name, StringName &r_global_class_name) const {
	if (p_namespace.is_empty()) {
		return false;
	}

	const String global_class_name = p_namespace + "." + String(p_class_name);
	if (!ScriptServer::is_global_class(global_class_name)) {
		return false;
	}

	r_global_class_name = global_class_name;
	return true;
}

bool FSAnalyzer::get_imported_global_class(const StringName &p_class_name, const FSParser::Node *p_source, StringName &r_global_class_name, bool &r_error, const String &p_symbol_kind) {
	r_error = false;
	String matched_import;
	LocalVector<String> checked_imports;

	for (const String &import : parser->head->imports) {
		if (_string_vector_has(checked_imports, import)) {
			continue;
		}
		checked_imports.push_back(import);

		StringName candidate;
		if (!get_global_class_in_namespace(import, p_class_name, candidate)) {
			continue;
		}

		if (r_global_class_name != StringName() && r_global_class_name != candidate) {
			push_error(vformat(R"(Could not resolve %s "%s": imported namespaces "%s" and "%s" are ambiguous.)", p_symbol_kind, p_class_name, matched_import, import), p_source);
			r_error = true;
			return true;
		}

		r_global_class_name = candidate;
		matched_import = import;
	}

	return r_global_class_name != StringName();
}

bool FSAnalyzer::get_namespace_global_class_from_type_chain(const Vector<FSParser::IdentifierNode *> &p_type_chain, const FSParser::Node *p_source, StringName &r_global_class_name, int &r_type_chain_size, bool &r_error, const String &p_symbol_kind) {
	r_error = false;
	r_type_chain_size = 0;
	if (p_type_chain.is_empty()) {
		return false;
	}

	for (int prefix_size = p_type_chain.size(); prefix_size >= 1; prefix_size--) {
		const String class_prefix = _join_identifier_chain(p_type_chain, 0, prefix_size);

		if (prefix_size > 1 && ScriptServer::is_global_class(class_prefix)) {
			r_global_class_name = class_prefix;
			r_type_chain_size = prefix_size;
			return true;
		}

		StringName namespace_candidate;
		if (get_global_class_in_namespace(parser->head->namespace_name, class_prefix, namespace_candidate)) {
			r_global_class_name = namespace_candidate;
			r_type_chain_size = prefix_size;
			return true;
		}

		StringName imported_candidate;
		bool import_error = false;
		if (get_imported_global_class(class_prefix, p_source, imported_candidate, import_error, p_symbol_kind)) {
			if (import_error) {
				r_error = true;
				return true;
			}

			r_global_class_name = imported_candidate;
			r_type_chain_size = prefix_size;
			return true;
		}
	}

	return false;
}

bool FSAnalyzer::is_namespace_chain_root_shadowed(FSParser::IdentifierNode *p_identifier) {
	// Keep this in sync with the non-global lookup precedence in reduce_identifier().
	const StringName &name = p_identifier->name;

	if (p_identifier->suite && p_identifier->suite->has_local(name)) {
		return true;
	}

	if (FSParser::get_builtin_type(name) < Variant::VARIANT_MAX || class_exists(name)) {
		return true;
	}

	List<FSParser::ClassNode *> script_classes;
	get_class_node_current_scope_classes(parser->current_class, &script_classes, p_identifier);
	for (FSParser::ClassNode *script_class : script_classes) {
		if ((script_class->identifier && script_class->identifier->name == name) || script_class->members_indices.has(name)) {
			return true;
		}
	}

	const StringName native = parser->current_class->base_type.native_type;
	if (class_exists(native)) {
		if (ClassDB::has_property(native, name)) {
			return true;
		}

		MethodInfo method_info;
		if (ClassDB::get_method_info(native, name, &method_info) || ClassDB::get_signal(native, name, &method_info)) {
			return true;
		}

		if (ClassDB::has_enum(native, name)) {
			return true;
		}

		bool valid = false;
		ClassDB::get_integer_constant(native, name, &valid);
		if (valid) {
			return true;
		}
	}

	if (!FSLanguage::get_singleton()->is_reserved_global_name(name)) {
		ensure_autoload_index_current();
		const FSAutoloadIndexEntry *autoload = autoload_index.get_by_name(name);
		if (autoload != nullptr && autoload->is_singleton) {
			return true;
		}
	}

	return false;
}

static const FSParser::Node *_trait_use_source(const FSParser::ClassNode::TraitUse &p_trait_use,
		const FSParser::ClassNode *p_owner) {
	if (!p_trait_use.name.is_empty()) {
		return p_trait_use.name[0];
	}
	return p_owner;
}

static const FSParser::Node *_trait_requirement_source(const FSParser::ClassNode *p_class,
		FSParser::ClassNode *p_trait) {
	for (const FSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		if (trait_use.resolved_trait == p_trait) {
			return _trait_use_source(trait_use, p_class);
		}
		if (trait_use.resolved_trait != nullptr && trait_use.resolved_trait->resolved_traits.has(p_trait)) {
			return _trait_use_source(trait_use, p_class);
		}
	}
	return p_class->identifier != nullptr ? static_cast<const FSParser::Node *>(p_class->identifier) : p_class;
}

FSParser::ClassNode *FSAnalyzer::resolve_nested_trait_reference(FSParser::ClassNode *p_base,
		const FSParser::ClassNode::TraitUse &p_trait_use, int p_chain_index,
		const FSParser::Node *p_source) {
	FSParser::ClassNode *current = p_base;
	for (int i = p_chain_index; i < p_trait_use.name.size(); i++) {
		const StringName &name = p_trait_use.name[i]->name;
		if (current == nullptr || !current->members_indices.has(name)) {
			push_error(vformat(R"(Could not resolve trait "%s".)", p_trait_use.to_string()), p_source);
			return nullptr;
		}

		resolve_class_member(current, name, p_source);
		FSParser::ClassNode::Member member = current->get_member(name);
		if (member.type != FSParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
			push_error(vformat(R"(Cannot use %s "%s" as a trait.)", member.get_type_name(), name), p_trait_use.name[i]);
			return nullptr;
		}
		current = member.m_class;
	}

	return current;
}

FSParser::ClassNode *FSAnalyzer::resolve_local_trait_reference(FSParser::ClassNode *p_owner,
		const FSParser::ClassNode::TraitUse &p_trait_use, const FSParser::Node *p_source,
		bool &r_found) {
	r_found = false;
	if (p_trait_use.name.is_empty()) {
		return nullptr;
	}

	const StringName &first = p_trait_use.name[0]->name;
	List<FSParser::ClassNode *> script_classes;
	get_class_node_current_scope_classes(p_owner, &script_classes, p_trait_use.name[0]);
	for (FSParser::ClassNode *script_class : script_classes) {
		FSParser::ClassNode *candidate = nullptr;

		if (script_class->identifier != nullptr && script_class->identifier->name == first) {
			candidate = script_class;
		} else if (script_class->members_indices.has(first)) {
			resolve_class_member(script_class, first, p_source);
			FSParser::ClassNode::Member member = script_class->get_member(first);
			if (member.type != FSParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
				push_error(vformat(R"(Cannot use %s "%s" as a trait.)", member.get_type_name(), first),
						p_trait_use.name[0]);
				r_found = true;
				return nullptr;
			}
			candidate = member.m_class;
		}

		if (candidate == nullptr) {
			continue;
		}

		r_found = true;
		candidate = resolve_nested_trait_reference(candidate, p_trait_use, 1, p_source);
		if (candidate == nullptr) {
			return nullptr;
		}
		if (!candidate->is_trait) {
			push_error(vformat(R"(Class "%s" cannot be used as a trait.)", _class_or_trait_name(candidate)),
					p_trait_use.name[0]);
			return nullptr;
		}
		return candidate;
	}

	return nullptr;
}

FSParser::ClassNode *FSAnalyzer::resolve_global_trait_reference(const StringName &p_global_class_name,
		const FSParser::Node *p_source) {
	if (!ScriptServer::is_global_class(p_global_class_name)) {
		return nullptr;
	}

	const String path = ScriptServer::get_global_class_path(p_global_class_name);
	if (!is_bootstrap_dependency_path_allowed(path)) {
		push_error(vformat(R"(Build task bootstrap cannot use global trait "%s" from "%s"; it is outside the provider bootstrap root "%s".)",
						   p_global_class_name, path, bootstrap_allowed_dependency_root),
				p_source);
		return nullptr;
	}

	FSParser::ClassNode *global_class = nullptr;
	if (FoundryScript::is_canonically_equal_paths(path, parser->script_path)) {
		global_class = parser->head;
	} else {
		Ref<FSParserRef> ref;
		Error err = dependency_parser_access.raise_depended_parser_for(path, FSParserRef::INHERITANCE_SOLVED, ref);
		if (ref.is_null()) {
			push_error(vformat(R"(Could not parse global trait "%s" from "%s".)", p_global_class_name, path), p_source);
			return nullptr;
		}
		if (err != OK) {
			push_error(vformat(R"(Could not resolve inheritance for global trait "%s" from "%s".)",
							   p_global_class_name, path),
					p_source);
			return nullptr;
		}
		global_class = ref->get_parser()->head;
	}

	if (global_class == nullptr) {
		return nullptr;
	}
	if (!global_class->is_trait) {
		push_error(vformat(R"(Class "%s" cannot be used as a trait.)", p_global_class_name), p_source);
		return nullptr;
	}
	return global_class;
}

FSParser::ClassNode *FSAnalyzer::resolve_trait_reference(FSParser::ClassNode *p_owner,
		FSParser::ClassNode::TraitUse &r_trait_use, const FSParser::Node *p_source) {
	if (r_trait_use.resolved_trait != nullptr) {
		return r_trait_use.resolved_trait;
	}

	bool found_local = false;
	FSParser::ClassNode *trait = resolve_local_trait_reference(p_owner, r_trait_use, p_source, found_local);
	if (found_local) {
		r_trait_use.resolved_trait = trait;
		return trait;
	}

	StringName namespace_global_class;
	bool namespace_error = false;
	int namespace_type_chain_size = 0;
	if (get_namespace_global_class_from_type_chain(r_trait_use.name, p_source, namespace_global_class,
				namespace_type_chain_size, namespace_error, "trait")) {
		if (namespace_error) {
			return nullptr;
		}
		trait = resolve_global_trait_reference(namespace_global_class, p_source);
		if (trait != nullptr) {
			trait = resolve_nested_trait_reference(trait, r_trait_use, namespace_type_chain_size, p_source);
		}
		if (trait != nullptr && !trait->is_trait) {
			push_error(vformat(R"(Class "%s" cannot be used as a trait.)", _class_or_trait_name(trait)), p_source);
			return nullptr;
		}
		r_trait_use.resolved_trait = trait;
		return trait;
	}

	if (r_trait_use.name.size() == 1) {
		const StringName &name = r_trait_use.name[0]->name;
		if (ScriptServer::is_global_class(name)) {
			trait = resolve_global_trait_reference(name, p_source);
			r_trait_use.resolved_trait = trait;
			return trait;
		}
	}

	push_error(vformat(R"(Could not resolve trait "%s".)", r_trait_use.to_string()), p_source);
	return nullptr;
}

bool FSAnalyzer::datatype_derives_from_datatype(FSParser::DataType p_type,
		const FSParser::DataType &p_base) {
	if (!p_type.is_set() || !p_base.is_set()) {
		return false;
	}

	if (is_type_compatible(p_base, p_type)) {
		return true;
	}

	while (p_type.is_set()) {
		if (p_base.kind == FSParser::DataType::NATIVE && p_type.kind == FSParser::DataType::NATIVE) {
			return p_type.native_type == p_base.native_type || ClassDB::is_parent_class(p_type.native_type, p_base.native_type);
		}

		if (p_base.kind == FSParser::DataType::CLASS &&
				p_type.kind == FSParser::DataType::CLASS &&
				p_type.class_type == p_base.class_type) {
			return true;
		}

		if (p_type.kind == FSParser::DataType::CLASS && p_type.class_type != nullptr) {
			resolve_class_inheritance(p_type.class_type);
			p_type = p_type.class_type->base_type;
			continue;
		}

		if (p_type.kind == FSParser::DataType::SCRIPT &&
				p_base.kind == FSParser::DataType::SCRIPT &&
				((!p_type.script_path.is_empty() && p_type.script_path == p_base.script_path) ||
						(p_type.script_type.is_valid() && p_type.script_type == p_base.script_type))) {
			return true;
		}

		if (p_type.kind == FSParser::DataType::SCRIPT) {
			if (!p_type.script_path.is_empty()) {
				Ref<FSParserRef> parser_ref = dependency_parser_access.depended_parser_for(p_type.script_path, FSParserRef::INHERITANCE_SOLVED);
				if (parser_ref.is_valid() && parser_ref->get_status() >= FSParserRef::INHERITANCE_SOLVED) {
					p_type = parser_ref->get_parser()->head->base_type;
					continue;
				}
			}

			if (p_type.script_type.is_valid()) {
				Ref<Script> base_script = p_type.script_type->get_base_script();
				if (base_script.is_valid()) {
					FSParser::DataType base_script_type;
					base_script_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
					base_script_type.kind = FSParser::DataType::SCRIPT;
					base_script_type.builtin_type = Variant::OBJECT;
					base_script_type.native_type = base_script->get_instance_base_type();
					base_script_type.script_type = base_script;
					base_script_type.script_path = base_script->get_path();
					p_type = base_script_type;
					continue;
				}

				FSParser::DataType native_base_type;
				native_base_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
				native_base_type.kind = FSParser::DataType::NATIVE;
				native_base_type.builtin_type = Variant::OBJECT;
				native_base_type.native_type = p_type.script_type->get_instance_base_type();
				p_type = native_base_type;
				continue;
			}
		}

		break;
	}

	return false;
}

bool FSAnalyzer::type_argument_satisfies_bound(const FSParser::DataType &p_argument, const FSParser::DataType &p_bound) {
	// A `Variant` bound imposes no requirement; any argument satisfies it.
	if (p_bound.is_variant()) {
		return true;
	}
	// A bound that is itself an (unsubstituted) type parameter — e.g. an outer-scope parameter the
	// caller could not bind — constrains the argument only by its own upper bound; an unbounded one
	// imposes nothing. Never fall through to the permissive general compatibility check below.
	if (p_bound.kind == FSParser::DataType::TYPE_PARAMETER) {
		if (p_bound.type_parameter_bound.is_empty()) {
			return true;
		}
		// A nullable marker on the parameter handle itself (`U?`) widens its effective bound, so
		// carry it onto the unwrapped bound before recursing.
		FSParser::DataType bound = p_bound.type_parameter_bound[0];
		bound.is_nullable = bound.is_nullable || p_bound.is_nullable;
		return type_argument_satisfies_bound(p_argument, bound);
	}
	if (p_argument.kind == FSParser::DataType::TYPE_PARAMETER) {
		// A bare type parameter is erased to `Variant` at runtime, so it satisfies a concrete bound
		// only when its own declared upper bound provably does. The same strict rules apply to that
		// bound, so recurse rather than fall back to the permissive general compatibility check.
		if (p_argument.type_parameter_bound.is_empty()) {
			return false;
		}
		// A nullable type-parameter argument (`U?`) stays nullable through the unwrapping, so the
		// strict-null guard below still sees it.
		FSParser::DataType argument = p_argument.type_parameter_bound[0];
		argument.is_nullable = argument.is_nullable || p_argument.is_nullable;
		return type_argument_satisfies_bound(argument, p_bound);
	}
	// A concrete `Variant` argument never satisfies a non-`Variant` bound.
	if (p_argument.is_variant()) {
		return false;
	}
	// Under strict null checks a nullable argument cannot satisfy a non-nullable bound: the bound
	// promises a non-null value while the argument admits null. This runs after the type-parameter
	// cases above so it compares fully-unwrapped concrete handles (a type-parameter bound that is
	// itself nullable, e.g. `T: U` with `U: RefCounted?`, has already been recursed into), and before
	// the trait/generic/derivation walks, which ignore `is_nullable`.
	if (strict_null_checks && p_argument.is_nullable && !p_bound.is_nullable) {
		return false;
	}
	// A trait bound is satisfied nominally: the argument must `use` the trait (directly, transitively,
	// or through an ancestor), never reach it by inheritance. Route to the dedicated trait check rather
	// than the derivation walk below, which would always reject a conforming `uses` class.
	if (p_bound.kind == FSParser::DataType::CLASS && p_bound.class_type != nullptr && p_bound.class_type->is_trait) {
		return type_satisfies_trait(p_argument, p_bound);
	}
	// A generic bound (e.g. `List[int]`) must be matched invariantly in its type arguments, which the
	// general compatibility walk enforces along the inheritance chain; nominal derivation alone would
	// wrongly accept a `Stack[String]`. Plain (unspecialized) bounds keep the cheaper derivation check.
	if (p_bound.has_type_arguments()) {
		return is_type_compatible(p_bound, p_argument, false);
	}
	return datatype_derives_from_datatype(p_argument, p_bound);
}

bool FSAnalyzer::class_satisfies_trait_base(FSParser::ClassNode *p_class, FSParser::ClassNode *p_trait) {
	if (p_class == nullptr || p_trait == nullptr) {
		return false;
	}
	if (!p_trait->extends_used) {
		return true;
	}
	return datatype_derives_from_datatype(p_class->base_type, p_trait->base_type);
}

bool FSAnalyzer::type_satisfies_trait(const FSParser::DataType &p_argument, const FSParser::DataType &p_trait_bound) {
	// Trait conformance is nominal: the argument (or any ancestor) must declare the trait via `uses`,
	// so every class along the inheritance chain needs its `resolved_traits` populated before the shared
	// conformance walk inspects it. This check can run mid-inheritance-resolution (e.g. a subclass that
	// `extends Box[Sword]`), before the trait-use pass has reached each ancestor, so resolve each one
	// here. Resolving is idempotent; a class already mid-resolution is skipped to avoid a spurious cyclic
	// error and gets populated by the in-flight pass anyway.
	HashSet<FSParser::ClassNode *> visited;
	for (FSParser::ClassNode *ancestor = p_argument.class_type; ancestor != nullptr && !visited.has(ancestor);) {
		visited.insert(ancestor);
		if (!ancestor->resolving_trait_uses) {
			resolve_trait_uses(ancestor);
		}
		ancestor = ancestor->base_type.kind == FSParser::DataType::CLASS ? ancestor->base_type.class_type : nullptr;
	}
	// Reuse the shared conformance machinery (`_class_has_trait`), which walks the inheritance chain
	// and also accounts for externally scripted trait uses. Call `FSTypeCompatibility::check` directly
	// so `is_type_compatible` can route trait targets here without recursion.
	FSTypeCompatibility::Options options;
	options.strict_dynamic = strict_dynamic_checks;
	options.strict_null = strict_null_checks;
	return FSTypeCompatibility::check(p_trait_bound, p_argument, options).compatible;
}

static bool _datatype_alpha_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b);

bool FSAnalyzer::find_trait_implementation(FSParser::ClassNode *p_class, const StringName &p_function_name,
		TraitMethodImplementation &r_implementation) {
	r_implementation = TraitMethodImplementation();
	FSParser::ClassNode *current_class = p_class;
	HashSet<FSParser::ClassNode *> visited_classes;
	while (current_class != nullptr) {
		if (visited_classes.has(current_class)) {
			break;
		}
		visited_classes.insert(current_class);

		if (current_class->is_builtin_conformance_shim) {
			const FSParser::DataType &self_type = current_class->get_datatype();
			if (self_type.kind == FSParser::DataType::BUILTIN && self_type.builtin_type != Variant::NIL &&
					Variant::has_builtin_method(self_type.builtin_type, p_function_name)) {
				r_implementation.method_info = Variant::get_builtin_method_info(self_type.builtin_type, p_function_name);
				r_implementation.method_info_source = vformat(R"(Implementation comes from builtin type "%s".)", Variant::get_type_name(self_type.builtin_type));
				r_implementation.has_method_info = true;
				return true;
			}
			return false;
		}

		if (current_class->has_function(p_function_name)) {
			FSParser::FunctionNode *function = current_class->get_member(p_function_name).function;
			if (!function->is_abstract) {
				if (parser->has_class(current_class)) {
					resolve_function_signature_in_class(function, current_class, function);
					r_implementation.function = function;
					r_implementation.owner_class = current_class;
				} else {
					r_implementation.method_info = function->info;
					r_implementation.method_info_source = _trait_method_info_source(current_class, function);
					r_implementation.has_method_info = true;
				}
				return true;
			}
		}

		resolve_class_inheritance(current_class);
		if (current_class->base_type.kind == FSParser::DataType::CLASS) {
			current_class = current_class->base_type.class_type;
		} else if (current_class->base_type.kind == FSParser::DataType::SCRIPT) {
			if (!current_class->base_type.script_path.is_empty()) {
				Ref<FSParserRef> base_parser_ref = dependency_parser_access.depended_parser_for(current_class->base_type.script_path, FSParserRef::INTERFACE_SOLVED);
				if (base_parser_ref.is_valid() && base_parser_ref->get_status() >= FSParserRef::INTERFACE_SOLVED) {
					current_class = base_parser_ref->get_parser()->head;
					continue;
				}
			}

			Ref<Script> script = current_class->base_type.script_type;
			while (script.is_valid()) {
				MethodInfo info = script->get_method_info(p_function_name);
				if (info.name == p_function_name) {
					r_implementation.method_info = info;
					if (!script->get_path().is_empty()) {
						r_implementation.method_info_source = vformat(R"(Implementation comes from "%s".)",
								_localize_script_path(script->get_path()));
					}
					r_implementation.has_method_info = true;
					return true;
				}
				script = script->get_base_script();
			}
			current_class = nullptr;
		} else {
			MethodInfo info;
			if (ClassDB::get_method_info(current_class->base_type.native_type, p_function_name, &info)) {
				r_implementation.method_info = info;
				r_implementation.method_info_source = vformat(R"(Implementation comes from native class "%s".)", current_class->base_type.native_type);
				r_implementation.has_method_info = true;
				return true;
			}
			current_class = nullptr;
		}
	}

	return false;
}

static bool _signature_type_involves_type_parameter(const FSParser::DataType &p_type) {
	if (p_type.kind == FSParser::DataType::TYPE_PARAMETER) {
		return true;
	}
	for (const FSParser::DataType &element : p_type.container_element_types) {
		if (_signature_type_involves_type_parameter(element)) {
			return true;
		}
	}
	for (const FSParser::DataType &argument : p_type.type_arguments) {
		if (_signature_type_involves_type_parameter(argument)) {
			return true;
		}
	}
	// A Callable/Signal signature can hide a type parameter in its parameter or return types
	// (e.g. `Callable[[U], void]`).
	for (const FSParser::DataType &parameter_type : p_type.method_parameter_types) {
		if (_signature_type_involves_type_parameter(parameter_type)) {
			return true;
		}
	}
	for (const FSParser::DataType &return_type : p_type.method_return_type) {
		if (_signature_type_involves_type_parameter(return_type)) {
			return true;
		}
	}
	return false;
}

// Deep structural (alpha-)equality of two resolved types. DataType::operator== ignores Callable/Signal
// method signatures (and compares nested container/type-argument elements via operator== too, so the
// blind spot is recursive), which would let `Callable[[U], void]` match `Callable[[int], int]`. This
// recurses through every sub-type so a trait-required generic signature is matched exactly.
static bool _datatype_alpha_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b) {
	if (!(p_a == p_b)) {
		return false;
	}
	if (p_a.has_method_signature != p_b.has_method_signature) {
		return false;
	}
	// AsyncCallable and plain Callable are distinct signatures, so a generic trait requiring an
	// async callable cannot be satisfied by a synchronous one (and vice versa).
	if (p_a.signature_is_async != p_b.signature_is_async) {
		return false;
	}
	if (p_a.container_element_types.size() != p_b.container_element_types.size() ||
			p_a.type_arguments.size() != p_b.type_arguments.size() ||
			p_a.method_parameter_types.size() != p_b.method_parameter_types.size() ||
			p_a.method_return_type.size() != p_b.method_return_type.size() ||
			p_a.type_parameter_bound.size() != p_b.type_parameter_bound.size()) {
		return false;
	}
	for (int i = 0; i < p_a.type_parameter_bound.size(); i++) {
		if (!_datatype_alpha_equal(p_a.type_parameter_bound[i], p_b.type_parameter_bound[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.container_element_types.size(); i++) {
		if (!_datatype_alpha_equal(p_a.container_element_types[i], p_b.container_element_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.type_arguments.size(); i++) {
		if (!_datatype_alpha_equal(p_a.type_arguments[i], p_b.type_arguments[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_parameter_types.size(); i++) {
		if (!_datatype_alpha_equal(p_a.method_parameter_types[i], p_b.method_parameter_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_return_type.size(); i++) {
		if (!_datatype_alpha_equal(p_a.method_return_type[i], p_b.method_return_type[i])) {
			return false;
		}
	}
	return true;
}

static bool _datatype_strict_identity_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b) {
	if (p_a.kind != p_b.kind ||
			p_a.is_nullable != p_b.is_nullable ||
			p_a.is_meta_type != p_b.is_meta_type ||
			p_a.is_type_handle_annotation != p_b.is_type_handle_annotation ||
			p_a.has_method_signature != p_b.has_method_signature ||
			p_a.signature_is_async != p_b.signature_is_async ||
			p_a.container_element_types.size() != p_b.container_element_types.size() ||
			p_a.type_arguments.size() != p_b.type_arguments.size() ||
			p_a.method_parameter_types.size() != p_b.method_parameter_types.size() ||
			p_a.method_return_type.size() != p_b.method_return_type.size() ||
			p_a.type_parameter_bound.size() != p_b.type_parameter_bound.size()) {
		return false;
	}

	bool equal = false;
	switch (p_a.kind) {
		case FSParser::DataType::VARIANT:
			equal = true;
			break;
		case FSParser::DataType::BUILTIN:
			equal = p_a.builtin_type == p_b.builtin_type;
			break;
		case FSParser::DataType::NATIVE:
		case FSParser::DataType::ENUM:
			equal = p_a.native_type == p_b.native_type;
			break;
		case FSParser::DataType::SCRIPT:
			equal = p_a.script_type == p_b.script_type;
			break;
		case FSParser::DataType::CLASS:
			equal = p_a.class_type == p_b.class_type ||
					(p_a.class_type != nullptr && p_b.class_type != nullptr &&
							p_a.class_type->fqcn == p_b.class_type->fqcn);
			break;
		case FSParser::DataType::TUPLE:
			equal = p_a.native_type == p_b.native_type && p_a.script_path == p_b.script_path &&
					p_a.tuple_field_names == p_b.tuple_field_names;
			break;
		case FSParser::DataType::TYPE_PARAMETER:
			equal = p_a.type_parameter_name == p_b.type_parameter_name &&
					p_a.type_parameter_scope == p_b.type_parameter_scope &&
					p_a.type_parameter_index == p_b.type_parameter_index;
			break;
		case FSParser::DataType::RESOLVING:
		case FSParser::DataType::UNRESOLVED:
			break;
	}
	if (!equal) {
		return false;
	}

	for (int i = 0; i < p_a.type_parameter_bound.size(); i++) {
		if (!_datatype_strict_identity_equal(p_a.type_parameter_bound[i], p_b.type_parameter_bound[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.container_element_types.size(); i++) {
		if (!_datatype_strict_identity_equal(p_a.container_element_types[i], p_b.container_element_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.type_arguments.size(); i++) {
		if (!_datatype_strict_identity_equal(p_a.type_arguments[i], p_b.type_arguments[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_parameter_types.size(); i++) {
		if (!_datatype_strict_identity_equal(p_a.method_parameter_types[i], p_b.method_parameter_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_return_type.size(); i++) {
		if (!_datatype_strict_identity_equal(p_a.method_return_type[i], p_b.method_return_type[i])) {
			return false;
		}
	}
	return true;
}

HashMap<StringName, FSParser::DataType> FSAnalyzer::trait_type_argument_substitution(FSParser::ClassNode *p_class, FSParser::ClassNode *p_trait) {
	HashMap<StringName, FSParser::DataType> bindings;
	if (p_class == nullptr || p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return bindings;
	}
	// Resolve `p_trait`'s use-site type arguments as seen from `p_class`. A directly-applied generic
	// trait (`uses Container[int]`) binds its parameters here; a transitive generic supertrait
	// (`uses Wrapper` where `Wrapper uses Storage[int]`) is bound by the intermediate trait that
	// applies it, so we recurse through the intermediate and compose the two substitutions.
	for (const FSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		FSParser::ClassNode *used_trait = trait_use.resolved_trait;
		if (used_trait == nullptr) {
			continue;
		}
		if (used_trait == p_trait) {
			if (trait_use.resolved_type_arguments.is_empty()) {
				continue;
			}
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
			// `used_trait` (transitively) applies `p_trait`. First find how `used_trait` binds
			// `p_trait`, then re-specialize those arguments with `p_class`'s binding of
			// `used_trait`'s own parameters (`uses Wrapper[int]` forwarding `T` into `Storage[T]`).
			HashMap<StringName, FSParser::DataType> inner = trait_type_argument_substitution(used_trait, p_trait);
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

bool FSAnalyzer::validate_trait_method_signature(FSParser::ClassNode *p_trait,
		FSParser::ClassNode *p_implementing_class, FSParser::FunctionNode *p_required_function,
		const TraitMethodImplementation &p_implementation,
		const HashMap<StringName, FSParser::DataType> &p_trait_substitution) {
	resolve_function_signature_in_class(p_required_function, p_trait, p_required_function);
	if (p_implementation.has_method_info) {
		return validate_trait_method_info_signature(p_trait, p_implementing_class, p_required_function, p_implementation,
				p_trait_substitution);
	}

	FSParser::FunctionNode *implementation_function = p_implementation.function;
	resolve_function_signature_in_class(implementation_function, p_implementation.owner_class, implementation_function);

	const StringName function_name = p_required_function->identifier->name;
	const String trait_method_name = _class_or_trait_name(p_trait) + "." + String(function_name) + "()";
	const FSParser::DataType implementation_self_type = _self_type_for_class(p_implementing_class);
	const bool required_is_coroutine = p_required_function->is_coroutine;
	const bool implementation_is_coroutine = implementation_function->is_coroutine;
	if (required_is_coroutine != implementation_is_coroutine) {
		if (required_is_coroutine) {
			push_error(vformat(R"*(The function "%s()" must be async because it implements async trait method "%s".)*",
							   function_name, trait_method_name),
					implementation_function);
		} else {
			push_error(vformat(R"*(The function "%s()" cannot be async because it implements synchronous trait method "%s".)*",
							   function_name, trait_method_name),
					implementation_function);
		}
		return false;
	}

	bool valid = p_required_function->is_static == implementation_function->is_static;

	// The trait's use-site bindings (`T := int`) specialize the required signature. A generic required
	// method may declare a type parameter that shadows a trait parameter by name; that method-scoped
	// parameter keeps its own identity, so drop any shadowed names before substituting.
	HashMap<StringName, FSParser::DataType> method_trait_substitution = p_trait_substitution;
	for (const FSParser::TypeParameterNode *required_type_parameter : p_required_function->type_parameters) {
		if (required_type_parameter != nullptr && required_type_parameter->identifier != nullptr) {
			method_trait_substitution.erase(required_type_parameter->identifier->name);
		}
	}

	// A trait may require a generic method; an implementation satisfies it up to type-parameter
	// renaming (alpha-equivalence by ordinal position). The lists must share arity, the bound at each
	// position must align, and the implementation's type parameters are renamed onto the required ones
	// so `map[V](...) -> Array[V]` matches required `map[U](...) -> Array[U]`. The renaming is applied
	// to the implementation's parameter/return types before the per-type compatibility checks below.
	HashMap<StringName, FSParser::DataType> type_parameter_renaming;
	{
		const Vector<FSParser::TypeParameterNode *> &required_type_parameters = p_required_function->type_parameters;
		const Vector<FSParser::TypeParameterNode *> &implementation_type_parameters = implementation_function->type_parameters;
		if (required_type_parameters.size() != implementation_type_parameters.size()) {
			valid = false;
		} else {
			// Method type parameters carry no eager resolved_bound, so read the bound TypeNode's
			// (meta-stripped) datatype.
			auto bound_of = [](const FSParser::TypeParameterNode *p_type_parameter) -> FSParser::DataType {
				FSParser::DataType bound;
				if (p_type_parameter != nullptr && p_type_parameter->bound != nullptr) {
					bound = p_type_parameter->bound->get_datatype();
					bound.is_meta_type = false;
				}
				return bound;
			};
			// Pass 1: build the renaming of the implementation's type parameters onto the required ones
			// (same method scope and ordinal index), carrying the required bound so the structural
			// equality used below — which compares type_parameter_bound — aligns after substitution.
			for (int i = 0; i < required_type_parameters.size(); i++) {
				const FSParser::TypeParameterNode *required_type_parameter = required_type_parameters[i];
				const FSParser::TypeParameterNode *implementation_type_parameter = implementation_type_parameters[i];
				if (required_type_parameter == nullptr || required_type_parameter->identifier == nullptr ||
						implementation_type_parameter == nullptr || implementation_type_parameter->identifier == nullptr) {
					continue;
				}
				FSParser::DataType required_handle;
				required_handle.kind = FSParser::DataType::TYPE_PARAMETER;
				required_handle.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
				required_handle.type_parameter_name = required_type_parameter->identifier->name;
				required_handle.type_parameter_scope = FSParser::DataType::TYPE_PARAMETER_METHOD;
				required_handle.type_parameter_index = i;
				const FSParser::DataType required_bound = _substitute_type_parameters_and_self(
						bound_of(required_type_parameter), method_trait_substitution, implementation_self_type);
				if (required_bound.is_set() && !required_bound.is_variant()) {
					required_handle.type_parameter_bound.push_back(required_bound);
				}
				type_parameter_renaming.insert(implementation_type_parameter->identifier->name, required_handle);
			}
			// Pass 2: each position's bound must align after renaming, so a dependent bound like
			// `[U: Resource, T: U]` matches `[V: Resource, W: V]`. A bound that involves a type
			// parameter is compared by alpha-equivalence (structural equality after renaming); a
			// concrete bound by ordinary mutual compatibility.
			for (int i = 0; i < required_type_parameters.size(); i++) {
				const FSParser::DataType required_bound = _substitute_type_parameters_and_self(
						bound_of(required_type_parameters[i]), method_trait_substitution, implementation_self_type);
				const FSParser::DataType implementation_bound = _substitute_type_parameters_and_self(
						bound_of(implementation_type_parameters[i]), type_parameter_renaming, implementation_self_type);
				const bool required_has_bound = required_bound.is_set() && !required_bound.is_variant();
				const bool implementation_has_bound = implementation_bound.is_set() && !implementation_bound.is_variant();
				if (required_has_bound != implementation_has_bound) {
					valid = false;
				} else if (required_has_bound) {
					// Bounds must be the same after renaming — compared by deep structural equality so a
					// difference hidden in a Callable/Signal bound signature is not lost.
					valid = valid && _datatype_alpha_equal(required_bound, implementation_bound);
				}
			}
		}
	}

	// A generic method's signature is matched by alpha-equivalence (exact structural match after
	// renaming), not the lenient subtype compatibility used for ordinary methods — otherwise a bare
	// type-parameter return (`-> U`) would be leniently accepted against `-> Array[U]`.
	const bool is_generic_method = !p_required_function->type_parameters.is_empty() || !implementation_function->type_parameters.is_empty();

	if (p_required_function->return_type != nullptr) {
		// Specialize the required signature with the generic trait's use-site arguments (`T := int`)
		// before comparing it to the implementation.
		const FSParser::DataType required_return_type = _substitute_type_parameters_and_self(
				p_required_function->get_datatype(), method_trait_substitution, implementation_self_type);
		const FSParser::DataType implementation_return_type = _substitute_type_parameters_and_self(
				implementation_function->get_datatype(), type_parameter_renaming, implementation_self_type);
		if (is_generic_method &&
				(_signature_type_involves_type_parameter(required_return_type) ||
						_signature_type_involves_type_parameter(implementation_return_type))) {
			// A type-parameter-involving return must match by alpha-equivalence (so `-> V` does not
			// leniently satisfy `-> Array[U]`); concrete returns keep their covariant matching below.
			valid = valid && _datatype_alpha_equal(required_return_type, implementation_return_type);
		} else if (implementation_return_type.is_variant()) {
			valid = valid && required_return_type.is_variant();
		} else if (implementation_return_type.kind == FSParser::DataType::BUILTIN &&
				implementation_return_type.builtin_type == Variant::NIL) {
			if (required_return_type.is_hard_type() &&
					!(required_return_type.kind == FSParser::DataType::BUILTIN &&
							required_return_type.builtin_type == Variant::NIL)) {
				valid = false;
			}
		} else if (required_return_type.is_set() && implementation_return_type.is_set()) {
			valid = valid && is_type_compatible(required_return_type, implementation_return_type);
		}
	}

	const int required_min_argc = p_required_function->parameters.size() - p_required_function->default_arg_values.size();
	const int required_max_argc = p_required_function->is_vararg() ? INT_MAX : p_required_function->parameters.size();
	const int implementation_min_argc = implementation_function->parameters.size() - implementation_function->default_arg_values.size();
	const int implementation_max_argc = implementation_function->is_vararg() ? INT_MAX : implementation_function->parameters.size();
	valid = valid && implementation_min_argc <= required_min_argc && required_max_argc <= implementation_max_argc;

	if (valid) {
		for (int i = 0; i < p_required_function->parameters.size() && i < implementation_function->parameters.size(); i++) {
			const FSParser::DataType required_parameter_type = _substitute_type_parameters_and_self(
					p_required_function->parameters[i]->datatype, method_trait_substitution, implementation_self_type);
			const FSParser::DataType implementation_parameter_type = _substitute_type_parameters_and_self(
					implementation_function->parameters[i]->datatype, type_parameter_renaming, implementation_self_type);
			if (is_generic_method &&
					(_signature_type_involves_type_parameter(required_parameter_type) ||
							_signature_type_involves_type_parameter(implementation_parameter_type))) {
				// A type-parameter-involving parameter must match by alpha-equivalence; concrete
				// parameters keep their contravariant matching below.
				valid = valid && _datatype_alpha_equal(required_parameter_type, implementation_parameter_type);
			} else if (required_parameter_type.is_variant() && required_parameter_type.is_hard_type()) {
				valid = valid && implementation_parameter_type.is_variant();
			} else if (implementation_parameter_type.is_set() && required_parameter_type.is_set()) {
				valid = valid && is_type_compatible(implementation_parameter_type, required_parameter_type);
			}
		}
	}

	if (!valid) {
		push_error(vformat(R"*(The function "%s()" signature does not match required trait method "%s".)*",
						   function_name, trait_method_name),
				implementation_function);
		return false;
	}

	return true;
}

bool FSAnalyzer::validate_trait_method_info_signature(FSParser::ClassNode *p_trait,
		FSParser::ClassNode *p_implementing_class, FSParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation,
		const HashMap<StringName, FSParser::DataType> &p_trait_substitution) {
	const StringName function_name = p_required_function->identifier->name;
	const String trait_method_name = _class_or_trait_name(p_trait) + "." + String(function_name) + "()";
	const FSParser::DataType implementation_self_type = _self_type_for_class(p_implementing_class);

	// A MethodInfo carries no generic type-parameter information, so a generic trait requirement
	// cannot be verified up to renaming through this path and is therefore not satisfiable by it.
	if (!p_required_function->type_parameters.is_empty()) {
		String message = vformat(R"*(The function "%s()" signature does not match required generic trait method "%s".)*", function_name, trait_method_name);
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	const bool required_is_coroutine = p_required_function->is_coroutine;
	const bool implementation_is_coroutine = (p_implementation.method_info.flags & METHOD_FLAG_ASYNC) != 0;
	if (required_is_coroutine != implementation_is_coroutine) {
		String message;
		if (required_is_coroutine) {
			message = vformat(R"*(The function "%s()" must be async because it implements async trait method "%s".)*",
					function_name, trait_method_name);
		} else {
			message = vformat(R"*(The function "%s()" cannot be async because it implements synchronous trait method "%s".)*",
					function_name, trait_method_name);
		}
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	bool valid = (p_required_function->is_static == ((p_implementation.method_info.flags & METHOD_FLAG_STATIC) != 0));

	if (p_required_function->return_type != nullptr) {
		const FSParser::DataType required_return_type = _substitute_type_parameters_and_self(
				p_required_function->get_datatype(), p_trait_substitution, implementation_self_type);
		FSParser::DataType implementation_return_type = type_from_property(p_implementation.method_info.return_val);
		if (implementation_return_type.is_variant()) {
			valid = valid && required_return_type.is_variant();
		} else if (required_return_type.is_set() && implementation_return_type.is_set()) {
			valid = valid && is_type_compatible(required_return_type, implementation_return_type);
		}
	}

	const int required_min_argc = p_required_function->parameters.size() - p_required_function->default_arg_values.size();
	const int required_max_argc = p_required_function->is_vararg() ? INT_MAX : p_required_function->parameters.size();
	const int implementation_min_argc = p_implementation.method_info.arguments.size() - p_implementation.method_info.default_arguments.size();
	const int implementation_max_argc = (p_implementation.method_info.flags & METHOD_FLAG_VARARG) ? INT_MAX : p_implementation.method_info.arguments.size();
	valid = valid && implementation_min_argc <= required_min_argc && required_max_argc <= implementation_max_argc;

	if (valid) {
		for (int i = 0; i < p_required_function->parameters.size() && i < p_implementation.method_info.arguments.size(); i++) {
			const FSParser::DataType required_parameter_type = _substitute_type_parameters_and_self(
					p_required_function->parameters[i]->datatype, p_trait_substitution, implementation_self_type);
			const FSParser::DataType implementation_parameter_type = type_from_property(
					p_implementation.method_info.arguments[i], true);
			if (required_parameter_type.is_variant() && required_parameter_type.is_hard_type()) {
				valid = valid && implementation_parameter_type.is_variant();
			} else if (implementation_parameter_type.is_set() && required_parameter_type.is_set()) {
				valid = valid && is_type_compatible(implementation_parameter_type, required_parameter_type);
			}
		}
	}

	if (!valid) {
		String message = vformat(R"*(The native function "%s()" signature does not match required trait method "%s".)*",
				function_name, trait_method_name);
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	return true;
}

static bool _trait_member_is_state(const FSParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case FSParser::ClassNode::Member::VARIABLE:
		case FSParser::ClassNode::Member::CONSTANT:
		case FSParser::ClassNode::Member::ENUM:
		case FSParser::ClassNode::Member::ENUM_VALUE:
		case FSParser::ClassNode::Member::SIGNAL:
			return true;
		default:
			return false;
	}
}

struct TraitMemberSource {
	FSParser::ClassNode *trait = nullptr;
	FSParser::ClassNode::Member member;
};

// Compares the declared types of a class member and a trait member it redeclares, ignoring
// `type_source`. DataType::operator== treats INFERRED/UNDETECTED operands as equal for parsing
// purposes, which would let an inferred-but-incompatible redeclaration (e.g. `var health = "x"`
// against a trait's `var health: int`) slip through, so the structural identity is compared here.
static bool _trait_state_type_is_compatible(const FSParser::DataType &p_trait_type, const FSParser::DataType &p_class_type) {
	// A genuinely untyped redeclaration can hold the trait's value, so it is not a conflict.
	if (p_trait_type.kind == FSParser::DataType::VARIANT || p_class_type.kind == FSParser::DataType::VARIANT) {
		return true;
	}
	if (p_trait_type.kind != p_class_type.kind) {
		return false;
	}
	switch (p_class_type.kind) {
		case FSParser::DataType::BUILTIN:
			return p_trait_type.builtin_type == p_class_type.builtin_type &&
					p_trait_type.container_element_types == p_class_type.container_element_types;
		case FSParser::DataType::NATIVE:
		case FSParser::DataType::ENUM:
			return p_trait_type.native_type == p_class_type.native_type;
		case FSParser::DataType::SCRIPT:
			return p_trait_type.script_type == p_class_type.script_type;
		case FSParser::DataType::CLASS:
			return p_trait_type.class_type == p_class_type.class_type ||
					(p_trait_type.class_type != nullptr && p_class_type.class_type != nullptr &&
							p_trait_type.class_type->fqcn == p_class_type.class_type->fqcn);
		default:
			return true;
	}
}

void FSAnalyzer::validate_trait_conflicts(FSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->resolved_traits.is_empty()) {
		return;
	}

	// Traits and abstract classes are allowed to defer implementation and disambiguation to a
	// concrete subclass, mirroring validate_trait_requirements, so they must not raise conflicts.
	if (p_class->is_trait || p_class->is_abstract) {
		return;
	}

	HashMap<StringName, TraitMemberSource> trait_methods;
	HashMap<StringName, TraitMemberSource> trait_state;

	for (FSParser::ClassNode *trait : p_class->resolved_traits) {
		resolve_class_interface(trait, p_class);

		for (const FSParser::ClassNode::Member &member : trait->members) {
			if (member.type != FSParser::ClassNode::Member::FUNCTION && !_trait_member_is_state(member)) {
				continue;
			}

			const StringName member_name = StringName(member.get_name());
			if (member_name == StringName()) {
				continue;
			}

			// A trait member that the class does not itself redeclare will be flattened
			// in, so it must not collide with a member of the implementer's base classes
			// or native base — the same diagnostic the class's own members would raise.
			// (A method overriding a base method is allowed, as for normal classes.)
			if (!p_class->has_member(member_name) &&
					check_class_member_name_conflict(p_class, member_name, member.get_source_node()) != OK) {
				continue;
			}

			if (member.type == FSParser::ClassNode::Member::FUNCTION) {
				if (member.function == nullptr) {
					continue;
				}

				if (p_class->has_member(member_name)) {
					const FSParser::ClassNode::Member class_member = p_class->get_member(member_name);
					if (class_member.type != FSParser::ClassNode::Member::FUNCTION) {
						push_error(vformat(R"*(Class "%s" redeclares trait method "%s()" from "%s" with a %s member.)*",
										   _class_or_trait_name(p_class), member_name, _class_or_trait_name(trait),
										   class_member.get_type_name()),
								class_member.get_source_node());
						continue;
					}

					if (!member.function->is_abstract) {
						TraitMethodImplementation implementation;
						implementation.function = class_member.function;
						implementation.owner_class = p_class;
						validate_trait_method_signature(trait, p_class, member.function, implementation,
								trait_type_argument_substitution(p_class, trait));
					}
					continue;
				}

				if (member.function->is_abstract) {
					continue;
				}

				bool inherited_method_shadows_trait = false;
				for (FSParser::DataType *base_type = &p_class->base_type;
						base_type != nullptr && base_type->kind == FSParser::DataType::CLASS;) {
					FSParser::ClassNode *base_class = base_type->class_type;
					if (base_class == nullptr) {
						break;
					}

					if (base_class->has_function(member_name)) {
						FSParser::ClassNode::Member base_member = base_class->get_member(member_name);
						if (base_member.function != nullptr && !base_member.function->is_abstract) {
							TraitMethodImplementation implementation;
							implementation.function = base_member.function;
							implementation.owner_class = base_class;
							validate_trait_method_signature(trait, p_class, member.function, implementation,
									trait_type_argument_substitution(p_class, trait));
							inherited_method_shadows_trait = true;
							break;
						}
					}

					resolve_class_inheritance(base_class);
					base_type = &base_class->base_type;
				}
				if (inherited_method_shadows_trait) {
					continue;
				}

				HashMap<StringName, TraitMemberSource>::Iterator previous = trait_methods.find(member_name);
				if (previous) {
					push_error(vformat(R"*(Trait method "%s()" from "%s" conflicts with trait method "%s()" from "%s"; override it in "%s" to disambiguate.)*",
									   member_name, _class_or_trait_name(previous->value.trait), member_name,
									   _class_or_trait_name(trait), _class_or_trait_name(p_class)),
							_trait_requirement_source(p_class, trait));
					continue;
				}

				TraitMemberSource source;
				source.trait = trait;
				source.member = member;
				trait_methods.insert(member_name, source);
				continue;
			}

			if (p_class->has_member(member_name)) {
				const FSParser::ClassNode::Member class_member = p_class->get_member(member_name);
				if (class_member.type == FSParser::ClassNode::Member::FUNCTION) {
					push_error(vformat(R"(Class "%s" redeclares trait member "%s" from "%s" with a function.)",
									   _class_or_trait_name(p_class), member_name, _class_or_trait_name(trait)),
							class_member.get_source_node());
					continue;
				}

				const FSParser::DataType trait_type = _substitute_type_parameters_and_self(
						member.get_datatype(), trait_type_argument_substitution(p_class, trait), _self_type_for_class(p_class));
				const FSParser::DataType class_type = class_member.get_datatype();
				if (!_trait_state_type_is_compatible(trait_type, class_type)) {
					push_error(vformat(R"(Class "%s" redeclares trait member "%s" from "%s" with incompatible type. Expected "%s", got "%s".)",
									   _class_or_trait_name(p_class), member_name, _class_or_trait_name(trait),
									   trait_type.to_string(), class_type.to_string()),
							class_member.get_source_node());
				}
				continue;
			}

			HashMap<StringName, TraitMemberSource>::Iterator previous = trait_state.find(member_name);
			if (previous) {
				push_error(vformat(R"(Trait member "%s" from "%s" conflicts with trait member "%s" from "%s"; redeclare it in "%s" with type "%s" to disambiguate.)",
								   member_name, _class_or_trait_name(previous->value.trait), member_name,
								   _class_or_trait_name(trait), _class_or_trait_name(p_class),
								   previous->value.member.get_datatype().to_string()),
						_trait_requirement_source(p_class, trait));
				continue;
			}

			TraitMemberSource source;
			source.trait = trait;
			source.member = member;
			trait_state.insert(member_name, source);
		}
	}
}

void FSAnalyzer::validate_trait_requirements(FSParser::ClassNode *p_class) {
	if (p_class->is_trait || p_class->is_abstract) {
		return;
	}

	// A trait's base class is an inheritance constraint. Abstract methods inherited
	// from that base are enforced by the normal base-class abstract checks once the
	// using class derives from it.
	// This pass validates trait requirements only. Concrete trait methods are not merged
	// into the using class, so a concrete method on one used trait does not implement
	// an abstract method required by another used trait.
	HashSet<StringName> missing_trait_methods;
	for (FSParser::ClassNode *trait : p_class->resolved_traits) {
		for (FSParser::ClassNode::Member member : trait->members) {
			if (member.type != FSParser::ClassNode::Member::FUNCTION ||
					member.function == nullptr || !member.function->is_abstract) {
				continue;
			}

			TraitMethodImplementation implementation;
			if (!find_trait_implementation(p_class, member.function->identifier->name, implementation)) {
				const StringName function_name = member.function->identifier->name;
				if (!missing_trait_methods.has(function_name)) {
					missing_trait_methods.insert(function_name);
					push_error(vformat(R"*(Class "%s" must implement trait method "%s.%s()".)*",
									   _class_or_trait_name(p_class), _class_or_trait_name(trait),
									   function_name),
							_trait_requirement_source(p_class, trait));
				}
				continue;
			}

			validate_trait_method_signature(trait, p_class, member.function, implementation,
					trait_type_argument_substitution(p_class, trait));
		}
	}
}

Error FSAnalyzer::validate_imports() {
	if (parser->head->imports.is_empty()) {
		return OK;
	}

	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);
	LocalVector<String> checked_imports;

	for (const String &import : parser->head->imports) {
		if (_string_vector_has(checked_imports, import)) {
			continue;
		}
		checked_imports.push_back(import);

		if (!validate_bootstrap_namespace_import(import, global_classes)) {
			continue;
		}
		if (!bootstrap_allowed_dependency_root.is_empty()) {
			continue;
		}

		// A namespace is a valid import target when it exposes any global class/trait or any
		// custom annotation declaration. Annotation-only libraries declare no `class_name`, so
		// they would otherwise be invisible to import validation.
		if (!_namespace_exists_in_global_classes(global_classes, import) &&
				!FSLanguage::get_singleton()->namespace_has_annotations(import)) {
			push_error(vformat(R"(Could not find imported namespace "%s".)", import), parser->head);
		}
	}

	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

Error FSAnalyzer::validate_annotation_declarations() {
	if (parser->head->annotation_declarations.is_empty()) {
		return OK;
	}

	FSLanguage *language = FSLanguage::get_singleton();
	HashSet<String> declared_in_file;

	for (FSParser::AnnotationDeclarationNode *declaration : parser->head->annotation_declarations) {
		if (declaration->identifier == nullptr) {
			continue;
		}

		const StringName short_name = declaration->identifier->name;

		// Built-in annotation names (registered with the leading `@`) stay reserved in the
		// annotation-symbol space so custom declarations can never shadow engine behavior.
		if (parser->valid_annotations.has(StringName("@" + String(short_name)))) {
			push_error(vformat(R"(Cannot declare custom annotation "%s": "@%s" is a built-in annotation.)", short_name, short_name), declaration);
			continue;
		}

		const String &qualified_name = declaration->qualified_name;
		if (qualified_name.is_empty()) {
			continue;
		}

		if (declared_in_file.has(qualified_name)) {
			push_error(vformat(R"(Duplicate annotation declaration "%s".)", qualified_name), declaration);
			continue;
		}
		declared_in_file.insert(qualified_name);

		// A canonical identity registered by two or more distinct files is a hard error: imports
		// could not disambiguate between the declarations.
		if (language != nullptr && language->is_duplicated_global_annotation(StringName(qualified_name))) {
			push_error(vformat(R"(Duplicate annotation declaration "%s": the same canonical annotation is declared in another file.)", qualified_name), declaration);
		}
	}

	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

#ifdef DEBUG_ENABLED
void FSAnalyzer::validate_mixed_namespace_directory() {
	if (parser->head->get_global_name() == StringName() || parser->script_path.is_empty()) {
		return;
	}
	if (parser->is_project_ignoring_warnings ||
			parser->warning_levels[FSWarning::MIXED_NAMESPACE_DIRECTORY] == FSWarning::IGNORE) {
		return;
	}

	const String current_path = FoundryScript::canonicalize_path(parser->script_path);
	const String current_dir = current_path.get_base_dir();
	if (current_dir.is_empty()) {
		return;
	}

	Vector<String> namespaces;
	namespaces.push_back(_get_namespace_warning_name(parser->head->namespace_name));

	_update_mixed_namespace_directory_cache();

	const Vector<MixedNamespaceDirectoryClass> *directory_classes = mixed_namespace_directory_cache.classes_by_directory.getptr(current_dir);
	if (directory_classes != nullptr) {
		for (const MixedNamespaceDirectoryClass &class_info : *directory_classes) {
			if (class_info.script_path == current_path) {
				continue;
			}

			if (!namespaces.has(class_info.namespace_name)) {
				namespaces.push_back(class_info.namespace_name);
			}
		}
	}

	if (namespaces.size() < 2) {
		return;
	}

	// FoundryScript warnings are source-file diagnostics, so this intentionally emits once per analyzed global script class.
	parser->push_warning(parser->head, FSWarning::MIXED_NAMESPACE_DIRECTORY, current_dir, _format_namespace_warning_list(namespaces));
}
#endif // DEBUG_ENABLED

Ref<FSParserRef> FSAnalyzer::DependencyParserAccess::ensure_cached_external_parser_for_class(const FSParser::ClassNode *p_class, const FSParser::ClassNode *p_from_class, const char *p_context, const FSParser::Node *p_source) {
	mark_dependency_phase_completed();

	// Delicate piece of code that intentionally doesn't use the FoundryScript cache or `get_depended_parser_for`.
	// Search dependencies for the parser that owns `p_class` and make a cache entry for it.
	// Required for how we store pointers to classes owned by other parser trees and need to call `resolve_class_member` and such on the same parser tree.
	// Since https://github.com/godotengine/godot/pull/94871 there can technically be multiple parsers for the same script in the same parser tree.
	// Even if unlikely, getting the wrong parser could lead to strange undefined behavior without errors.

	if (p_class == nullptr || analyzer == nullptr || analyzer->parser == nullptr) {
		return nullptr;
	}

	// The synthesized native-target stand-in is owned by this parser but absent from its class table; it
	// carries no members and its base is a native engine class, so no external parser backs it. Treat it
	// like a local class (null parser ref, no error) so witness-body member resolution against the native
	// surface doesn't trip the foreign-class lookup.
	if (p_class->is_native_conformance_shim || p_class->is_builtin_conformance_shim) {
		return nullptr;
	}

	if (HashMap<const FSParser::ClassNode *, Ref<FSParserRef>>::Iterator E = external_class_parser_cache.find(p_class)) {
		return E->value;
	}

	FSParser *owner_parser = analyzer->parser;
	if (owner_parser->has_class(p_class)) {
		return nullptr;
	}

	if (p_from_class == nullptr) {
		p_from_class = owner_parser->head;
	}

	Ref<FSParserRef> parser_ref;
	for (const FSParser::ClassNode *look_class = p_from_class; look_class != nullptr; look_class = look_class->base_type.class_type) {
		if (owner_parser->has_class(look_class)) {
			parser_ref = find_cached_external_parser_for_class(p_class, owner_parser);
			if (parser_ref.is_valid()) {
				break;
			}
		}

		if (HashMap<const FSParser::ClassNode *, Ref<FSParserRef>>::Iterator E = external_class_parser_cache.find(look_class)) {
			parser_ref = find_cached_external_parser_for_class(p_class, E->value);
			if (parser_ref.is_valid()) {
				break;
			}
		}

		String look_class_script_path = look_class->get_datatype().script_path;
		if (HashMap<String, Ref<FSParserRef>>::Iterator E = owner_parser->depended_parsers.find(look_class_script_path)) {
			parser_ref = find_cached_external_parser_for_class(p_class, E->value);
			if (parser_ref.is_valid()) {
				break;
			}
		}
	}

	if (parser_ref.is_null()) {
		analyzer->push_error(vformat(R"(Parser bug (please report): Could not find external parser for class "%s". (%s))", p_class->fqcn, p_context), p_source);
		// A null parser will be inserted into the cache, so this error won't spam for the same class.
		// This is ok, the values of external_class_parser_cache are not assumed to be valid references.
	}

	external_class_parser_cache.insert(p_class, parser_ref);
	return parser_ref;
}

Ref<FSParserRef> FSAnalyzer::DependencyParserAccess::find_cached_external_parser_for_class(const FSParser::ClassNode *p_class, const Ref<FSParserRef> &p_dependant_parser) {
	if (p_dependant_parser.is_null()) {
		return nullptr;
	}

	if (HashMap<const FSParser::ClassNode *, Ref<FSParserRef>>::Iterator E = p_dependant_parser->get_analyzer()->dependency_parser_access.external_class_parser_cache.find(p_class)) {
		if (E->value.is_valid()) {
			// Silently ensure it's parsed.
			raise_parser_to_status(E->value, FSParserRef::PARSED);
			if (E->value->get_parser()->has_class(p_class)) {
				return E->value;
			}
		}
	}

	if (p_dependant_parser->get_parser()->has_class(p_class)) {
		return p_dependant_parser;
	}

	// Silently ensure it's parsed.
	raise_parser_to_status(p_dependant_parser, FSParserRef::PARSED);
	return find_cached_external_parser_for_class(p_class, p_dependant_parser->get_parser());
}

Ref<FSParserRef> FSAnalyzer::DependencyParserAccess::find_cached_external_parser_for_class(const FSParser::ClassNode *p_class, FSParser *p_dependant_parser) {
	if (p_dependant_parser == nullptr) {
		return nullptr;
	}

	String script_path = p_class->get_datatype().script_path;
	if (HashMap<String, Ref<FSParserRef>>::Iterator E = p_dependant_parser->depended_parsers.find(script_path)) {
		if (E->value.is_valid()) {
			// Silently ensure it's parsed.
			raise_parser_to_status(E->value, FSParserRef::PARSED);
			if (E->value->get_parser()->has_class(p_class)) {
				return E->value;
			}
		}
	}

	for (KeyValue<String, Ref<FSParserRef>> &dep : p_dependant_parser->depended_parsers) {
		if (dep.value.is_null()) {
			continue;
		}
		raise_parser_to_status(dep.value, FSParserRef::PARSED);
		Ref<FSParserRef> found = find_cached_external_parser_for_class(p_class, dep.value->get_parser());
		if (found.is_valid()) {
			return found;
		}
	}

	return nullptr;
}

Ref<FoundryScript> FSAnalyzer::get_depended_shallow_script(const String &p_path, Error &r_error) {
	// To keep a local cache of the parser for resolving external nodes later.
	const String path = ResourceUID::ensure_path(p_path);
	if (!is_bootstrap_dependency_path_allowed(path)) {
		r_error = ERR_PARSE_ERROR;
		return Ref<FoundryScript>();
	}
	parser->get_depended_parser_for(path);
	Ref<FoundryScript> scr = FSCache::get_shallow_script(path, r_error, parser->script_path);
	return scr;
}

void FSAnalyzer::reduce_identifier_from_base_set_class(FSParser::IdentifierNode *p_identifier, FSParser::DataType p_identifier_datatype) {
	ERR_FAIL_NULL(p_identifier);

	p_identifier->set_datatype(p_identifier_datatype);
	if (p_identifier_datatype.class_type != nullptr && p_identifier_datatype.class_type->is_trait) {
		return;
	}
	Error err = OK;
	Ref<FoundryScript> scr = get_depended_shallow_script(p_identifier_datatype.script_path, err);
	if (err) {
		push_error(vformat(R"(Error while getting cache for script "%s".)", p_identifier_datatype.script_path), p_identifier);
		return;
	}
	p_identifier->reduced_value = scr->find_class(p_identifier_datatype.class_type->fqcn);
	p_identifier->is_constant = true;
}

void FSAnalyzer::reduce_identifier_from_base(FSParser::IdentifierNode *p_identifier, FSParser::DataType *p_base) {
	if (!p_identifier->get_datatype().has_no_type()) {
		return;
	}

	FSParser::DataType base;
	if (p_base == nullptr) {
		base = type_from_metatype(parser->current_class->get_datatype());
	} else {
		base = *p_base;
	}
	FSParser::DataType self_type = type_handle_represented_type(base);

	// A value of a constrained type parameter `[T: Bound]` exposes the members of its bound,
	// so member access on `T` is resolved against `Bound`.
	if (base.kind == FSParser::DataType::TYPE_PARAMETER && !base.type_parameter_bound.is_empty()) {
		const bool was_meta_type = base.is_meta_type;
		base = base.type_parameter_bound[0];
		base.is_meta_type = was_meta_type;
	}

	if (base.is_coroutine) {
		// Coroutine[T] is opaque: its only source operation is await, so it exposes no members. Leave
		// the identifier unresolved instead of resolving against the FSFunctionState skin, which
		// is neither registered nor exposed in ClassDB.
		return;
	}

	StringName name = p_identifier->name;

	if (base.kind == FSParser::DataType::ENUM) {
		if (base.is_meta_type) {
			if (base.enum_values.has(name)) {
				FSParser::DataType case_type = type_from_metatype(base);
				if (base.is_tagged_union) {
					const FSParser::DataType::EnumCasePayload *payload = base.get_enum_case_payload(name);
					if (payload != nullptr) {
						// An un-called payload case is a constructor pseudo-type, never a value. The
						// construction form is handled in reduce_call before this path is reached.
						case_type.is_pseudo_type = true;
						case_type.enum_case_name = name;
						p_identifier->set_datatype(case_type);
						push_error(vformat(R"*(Enum case "%s.%s" carries a payload and must be constructed, e.g. "%s.%s(...)".)*",
										   base.enum_type, name, base.enum_type, name),
								p_identifier);
						return;
					}
					// A payload-less case of a tagged union is a value of the union, not an integer
					// constant: it folds to the read-only `[tag]` singleton its case erases to.
					p_identifier->set_datatype(case_type);
					p_identifier->is_constant = true;
					p_identifier->reduced_value = fs_tagged_union_case_singleton(base.enum_values[name]);
					return;
				}
				p_identifier->set_datatype(case_type);
				p_identifier->is_constant = true;
				p_identifier->reduced_value = base.enum_values[name];
				return;
			}
		}

		FSParser::EnumNode *enum_declaration = resolve_enum_declaration(base, p_identifier);
		const int *function_index = enum_declaration != nullptr ? enum_declaration->functions_indices.getptr(name) : nullptr;
		if (function_index != nullptr && *function_index >= 0 && *function_index < enum_declaration->functions.size()) {
			FSParser::FunctionNode *function = enum_declaration->functions[*function_index];
			if (function != nullptr && function->is_static == base.is_meta_type) {
				FSParser::DataType callable_type = make_callable_type(function->info, function);
				callable_type.has_explicit_method_signature = true;
				p_identifier->set_datatype(callable_type);
				p_identifier->source = FSParser::IdentifierNode::MEMBER_FUNCTION;
				p_identifier->function_source = function;
				p_identifier->function_source_is_static = function->is_static;
				return;
			}
		}

		if (base.is_meta_type) {
			// Enum does not have this value or static function.
			return;
		}

		if (base.is_tagged_union) {
			// Payload fields exist per case, not on the union, so they are only reachable after the
			// case is known. Name the field explicitly when it belongs to some case of this union.
			bool is_payload_field = false;
			for (const KeyValue<StringName, FSParser::DataType::EnumCasePayload> &payload : base.enum_case_payloads) {
				if (payload.value.field_names.has(name)) {
					is_payload_field = true;
					break;
				}
			}
			if (is_payload_field) {
				push_error(vformat(R"*(Cannot access payload field "%s" on tagged union "%s" directly; it belongs to a single case, so match on the case first.)*",
								   name, base.enum_type),
						p_identifier);
			} else {
				push_error(vformat(R"*(Cannot get property "%s" from a value of tagged union "%s".)*", name, base.enum_type),
						p_identifier);
			}
			return;
		}

		push_error(R"(Cannot get property from enum value.)", p_identifier);
		return;
	}

	if (base.kind == FSParser::DataType::BUILTIN) {
		if (base.is_meta_type) {
			bool valid = false;

			if (Variant::has_constant(base.builtin_type, name)) {
				valid = true;

				const Variant constant_value = Variant::get_constant_value(base.builtin_type, name);

				p_identifier->is_constant = true;
				p_identifier->reduced_value = constant_value;
				p_identifier->set_datatype(type_from_variant(constant_value, p_identifier));
			}

			if (!valid) {
				const StringName enum_name = Variant::get_enum_for_enumeration(base.builtin_type, name);
				if (enum_name != StringName()) {
					valid = true;

					p_identifier->is_constant = true;
					p_identifier->reduced_value = Variant::get_enum_value(base.builtin_type, enum_name, name);
					p_identifier->set_datatype(make_builtin_enum_type(enum_name, base.builtin_type, false));
				}
			}

			if (!valid && Variant::has_enum(base.builtin_type, name)) {
				valid = true;

				p_identifier->set_datatype(make_builtin_enum_type(name, base.builtin_type, true));
			}

			if (!valid && base.is_hard_type()) {
				push_error(vformat(R"(Cannot find member "%s" in base "%s".)", name, base.to_string()), p_identifier);
			}
		} else {
			switch (base.builtin_type) {
				case Variant::NIL: {
					if (base.is_hard_type()) {
						push_error(vformat(R"(Cannot get property "%s" on a null object.)", name), p_identifier);
					}
					return;
				}
				case Variant::DICTIONARY: {
					FSParser::DataType dummy;
					dummy.kind = FSParser::DataType::VARIANT;
					p_identifier->set_datatype(dummy);
					return;
				}
				default: {
					Callable::CallError temp;
					Variant dummy;
					Variant::construct(base.builtin_type, dummy, nullptr, 0, temp);
					List<PropertyInfo> properties;
					dummy.get_property_list(&properties);
					for (const PropertyInfo &prop : properties) {
						if (prop.name == name) {
							p_identifier->set_datatype(type_from_property(prop));
							return;
						}
					}
					if (Variant::has_builtin_method(base.builtin_type, name)) {
						p_identifier->set_datatype(call_site_validation.explicit_callable_type_from_info(Variant::get_builtin_method_info(base.builtin_type, name)));
						return;
					}
					if (base.is_hard_type()) {
						push_error(vformat(R"(Cannot find member "%s" in base "%s".)", name, base.to_string()), p_identifier);
					}
				}
			}
		}
		return;
	}

	FSParser::ClassNode *base_class = base.class_type;
	List<FSParser::ClassNode *> script_classes;
	HashSet<FSParser::ClassNode *> trait_interface_classes;
	bool is_base = true;

	if (base_class != nullptr) {
		get_class_node_current_scope_classes(base_class, &script_classes, p_identifier);
		// Flattened trait members are reachable from a class that applies the trait
		// (directly or transitively), and from a trait that requires another trait.
		// They are treated as instance-accessible members of the using scope.
		if (base_class->is_trait || !base_class->used_traits.is_empty()) {
			resolve_trait_uses(base_class, p_identifier);
			for (FSParser::ClassNode *trait : base_class->resolved_traits) {
				if (script_classes.find(trait) == nullptr) {
					script_classes.push_back(trait);
				}
				trait_interface_classes.insert(trait);
			}
		}
	}

	bool is_constructor = base.is_meta_type && p_identifier->name == SNAME("new");

	for (FSParser::ClassNode *script_class : script_classes) {
		const bool is_trait_interface_class = trait_interface_classes.has(script_class);
		const bool can_access_instance_member = is_base || is_trait_interface_class;
		FSParser::EnumNode *enum_file_decl = script_class->is_enum_file ? script_class->enum_file_decl : nullptr;

		if (base.is_meta_type && enum_file_decl != nullptr && enum_file_decl->identifier != nullptr) {
			resolve_class_interface(script_class, p_identifier);
			FSParser::DataType enum_type = enum_file_decl->get_datatype();
			if (enum_type.is_set() && enum_type.kind != FSParser::DataType::RESOLVING) {
				if (enum_type.enum_values.has(name)) {
					FSParser::DataType case_type = type_from_metatype(enum_type);
					if (enum_type.is_tagged_union) {
						if (enum_type.get_enum_case_payload(name) != nullptr) {
							// A payload case reached through the declaring script handle has no
							// construction form: the case constructor is only spelled on the enum name.
							case_type.is_pseudo_type = true;
							case_type.enum_case_name = name;
							p_identifier->set_datatype(case_type);
							push_error(vformat(R"*(Enum case "%s.%s" carries a payload and must be constructed through the enum name, e.g. "%s.%s(...)".)*",
											   enum_file_decl->identifier->name, name, enum_file_decl->identifier->name, name),
									p_identifier);
							return;
						}
						// A payload-less case is a value of the union: the read-only `[tag]` singleton
						// its case erases to, not the bare ordinal.
						p_identifier->set_datatype(case_type);
						p_identifier->is_constant = true;
						p_identifier->reduced_value = fs_tagged_union_case_singleton(enum_type.enum_values[name]);
						p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
						return;
					}
					p_identifier->set_datatype(case_type);
					p_identifier->is_constant = true;
					p_identifier->reduced_value = enum_type.enum_values[name];
					p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
					return;
				}

				if (enum_file_decl->identifier->name == name) {
					p_identifier->set_datatype(enum_type);
					p_identifier->is_constant = true;
					p_identifier->reduced_value = enum_file_decl->dictionary;
					p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
					return;
				}
			}
		}

		if (p_base == nullptr && script_class->identifier && script_class->identifier->name == name) {
			reduce_identifier_from_base_set_class(p_identifier, script_class->get_datatype());
			if (script_class->outer != nullptr) {
				p_identifier->source = FSParser::IdentifierNode::MEMBER_CLASS;
			} else if (!script_class->qualified_global_name.is_empty()) {
				// A bare reference to a namespaced global root class (including a
				// self-reference) must be emitted by its qualified identity; the bare
				// name is not registered, so the compiler's name lookup would miss it.
				p_identifier->resolved_global_class = script_class->qualified_global_name;
			}
			return;
		}

		if (is_constructor) {
			name = "_init";
		}

		if (script_class->has_member(name)) {
			resolve_class_member(script_class, name, p_identifier);

			FSParser::ClassNode::Member member = script_class->get_member(name);
			switch (member.type) {
				case FSParser::ClassNode::Member::CONSTANT: {
					p_identifier->set_datatype(member.get_datatype());
					p_identifier->is_constant = true;
					p_identifier->reduced_value = member.constant->initializer->reduced_value;
					p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
					p_identifier->constant_source = member.constant;
					return;
				}

				case FSParser::ClassNode::Member::ENUM_VALUE: {
					p_identifier->set_datatype(member.get_datatype());
					p_identifier->is_constant = true;
					p_identifier->reduced_value = member.enum_value.value;
					p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
					return;
				}

				case FSParser::ClassNode::Member::ENUM: {
					p_identifier->set_datatype(member.get_datatype());
					p_identifier->is_constant = true;
					p_identifier->reduced_value = member.m_enum->dictionary;
					p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
					return;
				}

				case FSParser::ClassNode::Member::VARIABLE: {
					if (can_access_instance_member && (!base.is_meta_type || member.variable->is_static)) {
						p_identifier->set_datatype(substitute_member_type(
								member.get_datatype(), specialize_ancestor_type(base, script_class), nullptr, &self_type));
						p_identifier->source = member.variable->is_static ? FSParser::IdentifierNode::STATIC_VARIABLE : FSParser::IdentifierNode::MEMBER_VARIABLE;
						p_identifier->variable_source = member.variable;
						member.variable->usages += 1;
						return;
					}
				} break;

				case FSParser::ClassNode::Member::SIGNAL: {
					if (can_access_instance_member && !base.is_meta_type) {
						p_identifier->set_datatype(p_base == nullptr ? member.get_datatype() : call_site_validation.explicit_signal_type_from_node(member.signal));
						p_identifier->source = FSParser::IdentifierNode::MEMBER_SIGNAL;
						p_identifier->signal_source = member.signal;
						member.signal->usages += 1;
						return;
					}
				} break;

				case FSParser::ClassNode::Member::FUNCTION: {
					if (can_access_instance_member && (!base.is_meta_type || member.function->is_static || is_constructor)) {
						FSParser::DataType callable_type = make_callable_type(member.function->info, member.function);
						// Substitute the method's `T`-typed parameters and return through the inheritance
						// chain, so `IntList extends List[int]` sees `func get() -> T` as `-> int`. The
						// method's own type parameters shadow same-named class ones and are left intact.
						const FSParser::DataType specialized_base = specialize_ancestor_type(base, script_class);
						const FSParser::DataType parameter_self_type =
								member.function->is_static ? self_type : _self_type_parameter_from_bound(self_type);
						callable_type = substitute_member_type(
								callable_type, specialized_base, member.function, &parameter_self_type);
						callable_type.method_return_type.clear();
						callable_type.method_return_type.push_back(substitute_member_type(
								member.function->get_datatype(), specialized_base, member.function, &self_type));
						if (base_class != nullptr && script_class != base_class && !script_class->is_trait &&
								_datatype_container_element_contains_self_type_parameter(member.function->get_datatype())) {
							callable_type.method_return_is_erased_container = true;
						}
						if (p_base != nullptr) {
							callable_type.has_explicit_method_signature = true;
						}
						p_identifier->set_datatype(callable_type);
						p_identifier->source = FSParser::IdentifierNode::MEMBER_FUNCTION;
						p_identifier->function_source = member.function;
						p_identifier->function_source_is_static = member.function->is_static;
						return;
					}
				} break;

				case FSParser::ClassNode::Member::CLASS: {
					reduce_identifier_from_base_set_class(p_identifier, member.get_datatype());
					p_identifier->source = FSParser::IdentifierNode::MEMBER_CLASS;
					return;
				}

				case FSParser::ClassNode::Member::TUPLE: {
					// A tuple declaration is a type handle, so a qualified `Outer.Vec2` resolves like a
					// nested class. Specializing through the base keeps `(T, int)` bound when the
					// declaring class is generic.
					p_identifier->set_datatype(substitute_member_type(
							member.get_datatype(), specialize_ancestor_type(base, script_class), nullptr, &self_type));
					p_identifier->source = FSParser::IdentifierNode::MEMBER_CLASS;
					return;
				}

				default: {
					// Do nothing
				}
			}
		}

		if (is_base) {
			is_base = script_class->base_type.class_type != nullptr;
			if (!is_base && p_base != nullptr && trait_interface_classes.is_empty()) {
				break;
			}
		}
	}

	// Check non-FoundryScript scripts.
	Ref<Script> script_type = base.script_type;

	if (base_class == nullptr && script_type.is_valid()) {
		List<PropertyInfo> property_list;
		script_type->get_script_property_list(&property_list);

		for (const PropertyInfo &property_info : property_list) {
			if (property_info.name != p_identifier->name) {
				continue;
			}

			const FSParser::DataType property_type = FSAnalyzer::type_from_property(property_info, false, false);

			p_identifier->set_datatype(property_type);
			p_identifier->source = FSParser::IdentifierNode::MEMBER_VARIABLE;
			return;
		}

		MethodInfo method_info = script_type->get_method_info(p_identifier->name);

		if (method_info.name == p_identifier->name) {
			p_identifier->set_datatype(call_site_validation.explicit_callable_type_from_info(method_info));
			p_identifier->source = FSParser::IdentifierNode::MEMBER_FUNCTION;
			p_identifier->function_source_is_static = method_info.flags & METHOD_FLAG_STATIC;
			return;
		}

		List<MethodInfo> signal_list;
		script_type->get_script_signal_list(&signal_list);

		for (const MethodInfo &signal_info : signal_list) {
			if (signal_info.name != p_identifier->name) {
				continue;
			}

			// Reconstruct the full signature (routing each argument through type_from_property) so a
			// directly-accessed external signal member keeps its parameter types — and any nested
			// callable/signal hint — for emit()/connect() compatibility checks. Plain make_signal_type
			// would leave the signature empty and erase it back to untyped at the script-API boundary.
			FSParser::DataType signal_type = call_site_validation.explicit_signal_type_from_info(signal_info);
			if (!_signature_is_comparison_safe(signal_type.method_parameter_types)) {
				// A user-class/enum slot rebuilt from PropertyInfo cannot be compared reliably as a rich
				// explicit signature (it surfaces as SCRIPT while a local annotation may be a CLASS handle).
				// Fall back to the MethodInfo form so compatibility is decided by class name instead.
				signal_type = make_signal_type(signal_info);
			}

			p_identifier->set_datatype(signal_type);
			p_identifier->source = FSParser::IdentifierNode::MEMBER_SIGNAL;
			return;
		}

		HashMap<StringName, Variant> constant_map;
		script_type->get_constants(&constant_map);

		if (constant_map.has(p_identifier->name)) {
			Variant constant = constant_map.get(p_identifier->name);

			p_identifier->set_datatype(make_builtin_meta_type(constant.get_type()));
			p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
			return;
		}
	}

	// Check native members. No need for native class recursion because Node exposes all Object's properties.
	const StringName &native = base.native_type;

	if (class_exists(native)) {
		if (is_constructor) {
			name = "_init";
		}

		MethodInfo method_info;
		if (ClassDB::has_property(native, name)) {
			StringName getter_name = ClassDB::get_property_getter(native, name);
			MethodBind *getter = ClassDB::get_method(native, getter_name);
			if (getter != nullptr) {
				bool has_setter = ClassDB::get_property_setter(native, name) != StringName();
				p_identifier->set_datatype(type_from_property(getter->get_return_info(), false, !has_setter));
				p_identifier->source = FSParser::IdentifierNode::INHERITED_VARIABLE;
			}
			return;
		}
		if (ClassDB::get_method_info(native, name, &method_info)) {
			// Method is callable.
			p_identifier->set_datatype(call_site_validation.explicit_callable_type_from_info(method_info));
			p_identifier->source = FSParser::IdentifierNode::INHERITED_VARIABLE;
			return;
		}
		if (ClassDB::get_signal(native, name, &method_info)) {
			// Signal is a type too.
			p_identifier->set_datatype(call_site_validation.explicit_signal_type_from_info(method_info));
			p_identifier->source = FSParser::IdentifierNode::INHERITED_VARIABLE;
			return;
		}
		if (ClassDB::has_enum(native, name)) {
			p_identifier->set_datatype(make_native_enum_type(name, native));
			p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;
			return;
		}
		bool valid = false;

		int64_t int_constant = ClassDB::get_integer_constant(native, name, &valid);
		if (valid) {
			p_identifier->is_constant = true;
			p_identifier->reduced_value = int_constant;
			p_identifier->source = FSParser::IdentifierNode::MEMBER_CONSTANT;

			// Check whether this constant, which exists, belongs to an enum
			StringName enum_name = ClassDB::get_integer_constant_enum(native, name);
			if (enum_name != StringName()) {
				p_identifier->set_datatype(make_native_enum_type(enum_name, native, false));
			} else {
				p_identifier->set_datatype(type_from_variant(int_constant, p_identifier));
			}
		}
	}
}

void FSAnalyzer::reduce_identifier(FSParser::IdentifierNode *p_identifier, bool can_be_builtin) {
	// TODO: This is an opportunity to further infer types.

	// Check if we are inside an enum. This allows enum values to access other elements of the same enum.
	if (current_enum) {
		for (int i = 0; i < current_enum->values.size(); i++) {
			const FSParser::EnumNode::Value &element = current_enum->values[i];
			if (element.identifier->name == p_identifier->name) {
				FSParser::DataType type;
				if (current_enum->get_datatype().is_set()) {
					type = type_from_metatype(current_enum->get_datatype());
				} else {
					StringName enum_name = current_enum->identifier ? current_enum->identifier->name : UNNAMED_ENUM;
					type = make_class_enum_type(enum_name, parser->current_class, parser->script_path, false);
					if (element.parent_enum->identifier) {
						type.enum_type = element.parent_enum->identifier->name;
					}
				}
				p_identifier->set_datatype(type);

				if (current_enum->is_tagged_union) {
					if (element.has_payload()) {
						push_error(vformat(R"*(Enum case "%s" carries a payload and must be constructed, e.g. "%s(...)".)*",
										   element.identifier->name, element.identifier->name),
								p_identifier);
					} else {
						// A tagged-union case is a value, not an integer constant: it folds to the
						// read-only `[tag]` singleton its case erases to.
						p_identifier->is_constant = true;
						p_identifier->reduced_value = fs_tagged_union_case_singleton(element.value);
					}
					return;
				}

				if (element.resolved) {
					p_identifier->is_constant = true;
					p_identifier->reduced_value = element.value;
				} else {
					push_error(R"(Cannot use another enum element before it was declared.)", p_identifier);
				}
				return; // Found anyway.
			}
		}
	}

	bool found_source = false;
	// Check if identifier is local.
	// If that's the case, the declaration already was solved before.
	switch (p_identifier->source) {
		case FSParser::IdentifierNode::FUNCTION_PARAMETER:
			p_identifier->set_datatype(p_identifier->parameter_source->get_datatype());
			found_source = true;
			break;
		case FSParser::IdentifierNode::LOCAL_CONSTANT:
		case FSParser::IdentifierNode::MEMBER_CONSTANT:
			p_identifier->set_datatype(p_identifier->constant_source->get_datatype());
			p_identifier->is_constant = true;
			// TODO: Constant should have a value on the node itself.
			p_identifier->reduced_value = p_identifier->constant_source->initializer->reduced_value;
			found_source = true;
			break;
		case FSParser::IdentifierNode::MEMBER_SIGNAL:
			p_identifier->signal_source->usages++;
			[[fallthrough]];
		case FSParser::IdentifierNode::INHERITED_VARIABLE:
			mark_lambda_use_self();
			break;
		case FSParser::IdentifierNode::MEMBER_VARIABLE:
			mark_lambda_use_self();
			p_identifier->variable_source->usages++;
			[[fallthrough]];
		case FSParser::IdentifierNode::STATIC_VARIABLE:
		case FSParser::IdentifierNode::LOCAL_VARIABLE:
			p_identifier->set_datatype(p_identifier->variable_source->get_datatype());
			found_source = true;
#ifdef DEBUG_ENABLED
			if (p_identifier->variable_source && p_identifier->variable_source->assignments == 0 && !(p_identifier->get_datatype().is_hard_type() && p_identifier->get_datatype().kind == FSParser::DataType::BUILTIN)) {
				parser->push_warning(p_identifier, FSWarning::UNASSIGNED_VARIABLE, p_identifier->name);
			}
#endif // DEBUG_ENABLED
			break;
		case FSParser::IdentifierNode::LOCAL_ITERATOR:
			p_identifier->set_datatype(p_identifier->bind_source->get_datatype());
			found_source = true;
			break;
		case FSParser::IdentifierNode::LOCAL_BIND: {
			FSParser::DataType result = p_identifier->bind_source->get_datatype();
			result.is_constant = true;
			p_identifier->set_datatype(result);
			found_source = true;
		} break;
		case FSParser::IdentifierNode::UNDEFINED_SOURCE:
		case FSParser::IdentifierNode::MEMBER_FUNCTION:
		case FSParser::IdentifierNode::MEMBER_CLASS:
		case FSParser::IdentifierNode::NATIVE_CLASS:
			break;
	}

	if (found_source) {
		const FSParser::Node *flow_key = flow_finality.flow_narrowing_key_from_identifier(p_identifier);
		if (flow_key != nullptr) {
			if (const FSParser::DataType *narrowed_type = flow_finality.lookup_flow_narrowed_type(flow_key)) {
				p_identifier->set_datatype(*narrowed_type);
			}
		}
	}

#ifdef DEBUG_ENABLED
	if (!found_source && p_identifier->suite != nullptr && p_identifier->suite->has_local(p_identifier->name)) {
		parser->push_warning(p_identifier, FSWarning::CONFUSABLE_LOCAL_USAGE, p_identifier->name);
	}
#endif // DEBUG_ENABLED

	// Not a local, so check members.

	if (!found_source) {
		reduce_identifier_from_base(p_identifier);
		if (p_identifier->source != FSParser::IdentifierNode::UNDEFINED_SOURCE || p_identifier->get_datatype().is_set()) {
			// Found.
			found_source = true;
		}
	}

	if (found_source) {
		const bool source_is_instance_variable = p_identifier->source == FSParser::IdentifierNode::MEMBER_VARIABLE || p_identifier->source == FSParser::IdentifierNode::INHERITED_VARIABLE;
		const bool source_is_instance_function = p_identifier->source == FSParser::IdentifierNode::MEMBER_FUNCTION && !p_identifier->function_source_is_static;
		const bool source_is_signal = p_identifier->source == FSParser::IdentifierNode::MEMBER_SIGNAL;

		const FSParser::FunctionNode *enum_function = parser->current_function;
		while (enum_function != nullptr && enum_function->owner_enum == nullptr && enum_function->source_lambda != nullptr) {
			enum_function = enum_function->source_lambda->parent_function;
		}
		if (enum_function != nullptr && enum_function->owner_enum != nullptr &&
				(source_is_instance_variable || source_is_instance_function || source_is_signal)) {
			push_error(vformat(
							   R"*(Enum function "%s()" cannot access containing class instance member "%s".)*",
							   enum_function->identifier->name, p_identifier->name),
					p_identifier);
			return;
		}

		if (static_context && (source_is_instance_variable || source_is_instance_function || source_is_signal)) {
			// Get the parent function above any lambda.
			FSParser::FunctionNode *parent_function = parser->current_function;
			while (parent_function && parent_function->source_lambda) {
				parent_function = parent_function->source_lambda->parent_function;
			}

			String source_type;
			if (source_is_instance_variable) {
				source_type = "non-static variable";
			} else if (source_is_instance_function) {
				source_type = "non-static function";
			} else { // source_is_signal
				source_type = "signal";
			}

			if (parent_function) {
				push_error(vformat(R"*(Cannot access %s "%s" from the static function "%s()".)*", source_type, p_identifier->name, parent_function->identifier->name), p_identifier);
			} else {
				push_error(vformat(R"*(Cannot access %s "%s" from a static variable initializer.)*", source_type, p_identifier->name), p_identifier);
			}
		}

		if (current_lambda != nullptr) {
			// If the identifier is a member variable (including the native class properties), member function, or a signal,
			// we consider the lambda to be using `self`, so we keep a reference to the current instance.
			if (source_is_instance_variable || source_is_instance_function || source_is_signal) {
				mark_lambda_use_self();
				return; // No need to capture.
			}

			switch (p_identifier->source) {
				case FSParser::IdentifierNode::FUNCTION_PARAMETER:
				case FSParser::IdentifierNode::LOCAL_VARIABLE:
				case FSParser::IdentifierNode::LOCAL_ITERATOR:
				case FSParser::IdentifierNode::LOCAL_BIND:
					break; // Need to capture.
				case FSParser::IdentifierNode::UNDEFINED_SOURCE: // A global.
				case FSParser::IdentifierNode::LOCAL_CONSTANT:
				case FSParser::IdentifierNode::MEMBER_VARIABLE:
				case FSParser::IdentifierNode::MEMBER_CONSTANT:
				case FSParser::IdentifierNode::MEMBER_FUNCTION:
				case FSParser::IdentifierNode::MEMBER_SIGNAL:
				case FSParser::IdentifierNode::MEMBER_CLASS:
				case FSParser::IdentifierNode::INHERITED_VARIABLE:
				case FSParser::IdentifierNode::STATIC_VARIABLE:
				case FSParser::IdentifierNode::NATIVE_CLASS:
					return; // No need to capture.
			}

			FSParser::FunctionNode *function_test = current_lambda->function;
			// Make sure we aren't capturing variable in the same lambda.
			// This also add captures for nested lambdas.
			while (function_test != nullptr && function_test != p_identifier->source_function && function_test->source_lambda != nullptr && !function_test->source_lambda->captures_indices.has(p_identifier->name)) {
				function_test->source_lambda->captures_indices[p_identifier->name] = function_test->source_lambda->captures.size();
				function_test->source_lambda->captures.push_back(p_identifier);
				flow_finality.mark_flow_narrowing_capture(p_identifier);
				function_test = function_test->source_lambda->parent_function;
			}
		}

		return;
	}

	StringName name = p_identifier->name;
	p_identifier->source = FSParser::IdentifierNode::UNDEFINED_SOURCE;

	// Not a local or a member, so check globals.

	Variant::Type builtin_type = FSParser::get_builtin_type(name);
	if (builtin_type < Variant::VARIANT_MAX) {
		if (can_be_builtin) {
			p_identifier->set_datatype(make_builtin_meta_type(builtin_type));
			return;
		} else {
			push_error(R"(Builtin type cannot be used as a name on its own.)", p_identifier);
		}
	}

	if (class_exists(name)) {
		p_identifier->source = FSParser::IdentifierNode::NATIVE_CLASS;
		p_identifier->set_datatype(make_native_meta_type(name));
		return;
	}

	FSParser::DataType autoload_singleton_type;
	if (get_autoload_singleton_value_type(name, autoload_singleton_type)) {
		p_identifier->set_datatype(autoload_singleton_type);
		return;
	}

	StringName namespace_global_class;
	bool namespace_error = false;
	if (get_global_class_in_namespace(parser->head->namespace_name, name, namespace_global_class) ||
			get_imported_global_class(name, p_identifier, namespace_global_class, namespace_error)) {
		if (namespace_error) {
			FSParser::DataType dummy;
			dummy.kind = FSParser::DataType::VARIANT;
			p_identifier->set_datatype(dummy);
			return;
		}
		if (reject_bootstrap_global_class_dependency(namespace_global_class, p_identifier, "global class")) {
			FSParser::DataType dummy;
			dummy.kind = FSParser::DataType::VARIANT;
			p_identifier->set_datatype(dummy);
			return;
		}
		if (ScriptServer::is_global_class_enum(namespace_global_class)) {
			const String path = ScriptServer::get_global_class_path(namespace_global_class);
			set_enum_meta_identifier_constant(p_identifier, make_global_enum_type_from_path(namespace_global_class, path, p_identifier));
		} else {
			p_identifier->set_datatype(make_global_class_meta_type(namespace_global_class, p_identifier));
			p_identifier->resolved_global_class = namespace_global_class;
		}
		return;
	}

	if (ScriptServer::is_global_class(name)) {
		if (reject_bootstrap_global_class_dependency(name, p_identifier, "global class")) {
			FSParser::DataType dummy;
			dummy.kind = FSParser::DataType::VARIANT;
			p_identifier->set_datatype(dummy);
			return;
		}
		if (ScriptServer::is_global_class_enum(name)) {
			const String path = ScriptServer::get_global_class_path(name);
			set_enum_meta_identifier_constant(p_identifier, make_global_enum_type_from_path(name, path, p_identifier));
		} else {
			p_identifier->set_datatype(make_global_class_meta_type(name, p_identifier));
		}
		return;
	}

	if (CoreConstants::is_global_constant(name)) {
		int index = CoreConstants::get_global_constant_index(name);
		StringName enum_name = CoreConstants::get_global_constant_enum(index);
		int64_t value = CoreConstants::get_global_constant_value(index);
		if (enum_name != StringName()) {
			p_identifier->set_datatype(make_global_enum_type(enum_name, StringName(), false));
		} else {
			p_identifier->set_datatype(type_from_variant(value, p_identifier));
		}
		p_identifier->is_constant = true;
		p_identifier->reduced_value = value;
		return;
	}

	if (FSLanguage::get_singleton()->has_any_global_constant(name)) {
		Variant constant = FSLanguage::get_singleton()->get_any_global_constant(name);
		p_identifier->set_datatype(type_from_variant(constant, p_identifier));
		p_identifier->is_constant = true;
		p_identifier->reduced_value = constant;
		return;
	}

	if (CoreConstants::is_global_enum(name)) {
		p_identifier->set_datatype(make_global_enum_type(name, StringName(), true));
		if (!can_be_builtin) {
			push_error(vformat(R"(Global enum "%s" cannot be used on its own.)", name), p_identifier);
		}
		return;
	}

	if (Variant::has_utility_function(name) || FSUtilityFunctions::function_exists(name)) {
		p_identifier->is_constant = true;
		p_identifier->reduced_value = Callable(memnew(FSUtilityCallable(name)));
		MethodInfo method_info;
		if (FSUtilityFunctions::function_exists(name)) {
			method_info = FSUtilityFunctions::get_function_info(name);
		} else {
			method_info = Variant::get_utility_function_info(name);
		}
		p_identifier->set_datatype(make_callable_type(method_info));
		return;
	}

	// Allow "Variant" here since it might be used for nested enums.
	if (can_be_builtin && name == SNAME("Variant")) {
		FSParser::DataType variant;
		variant.kind = FSParser::DataType::VARIANT;
		variant.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		variant.is_meta_type = true;
		variant.is_pseudo_type = true;
		p_identifier->set_datatype(variant);
		return;
	}

	// Not found.
	push_error(vformat(R"(Identifier "%s" not declared in the current scope.)", name), p_identifier);
	FSParser::DataType dummy;
	dummy.kind = FSParser::DataType::VARIANT;
	p_identifier->set_datatype(dummy); // Just so type is set to something.
}

void FSAnalyzer::reduce_lambda(FSParser::LambdaNode *p_lambda) {
	// Lambda is always a Callable.
	FSParser::DataType lambda_type;
	lambda_type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
	lambda_type.kind = FSParser::DataType::BUILTIN;
	lambda_type.builtin_type = Variant::CALLABLE;
	p_lambda->set_datatype(lambda_type);

	if (p_lambda->function == nullptr) {
		return;
	}

	FSParser::LambdaNode *previous_lambda = current_lambda;
	current_lambda = p_lambda;
	resolve_function_signature(p_lambda->function, p_lambda, true);
	current_lambda = previous_lambda;

	lambda_type = make_callable_type(p_lambda->function->info, p_lambda->function);
	lambda_type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
	lambda_type.is_constant = false;
	p_lambda->set_datatype(lambda_type);

	pending_body_resolution_lambdas.push_back(p_lambda);
}

void FSAnalyzer::reduce_literal(FSParser::LiteralNode *p_literal) {
	p_literal->reduced_value = p_literal->value;
	p_literal->is_constant = true;

	p_literal->set_datatype(type_from_variant(p_literal->reduced_value, p_literal));
}

void FSAnalyzer::reduce_preload(FSParser::PreloadNode *p_preload) {
	if (!p_preload->path) {
		return;
	}

	auto finalize_preload = [&]() {
		p_preload->is_constant = true;
		p_preload->reduced_value = p_preload->resource;
		p_preload->set_datatype(type_from_variant(p_preload->reduced_value, p_preload));

		// TODO: Not sure if this is necessary anymore.
		// 'type_from_variant()' should call 'resolve_class_inheritance()' which would call 'dependency_parser_access.ensure_cached_external_parser_for_class()'
		// Better safe than sorry.
		dependency_parser_access.ensure_cached_external_parser_for_class(p_preload->get_datatype().class_type, nullptr, "Trying to resolve preload", p_preload);
	};

	auto raise_preloaded_script_conformances = [&]() {
		const String depended_path = ResourceUID::ensure_path(p_preload->resolved_path);
		Ref<FSParserRef> depended_ref;
		if (dependency_parser_access.raise_depended_parser_for(depended_path, FSParserRef::PARSED, depended_ref) == OK) {
			const FSParser *depended_parser = depended_ref->get_parser();
			if (depended_parser != nullptr && depended_parser->head != nullptr && !depended_parser->head->conformances.is_empty()) {
				dependency_parser_access.raise_parser_to_status(depended_ref, FSParserRef::INTERFACE_SOLVED);
			}
		}
	};

	reduce_expression(p_preload->path);

	if (!p_preload->path->is_constant) {
		push_error("Preloaded path must be a constant string.", p_preload->path);
		return;
	}

	if (p_preload->path->reduced_value.get_type() != Variant::STRING) {
		push_error("Preloaded path must be a constant string.", p_preload->path);
	} else {
		p_preload->resolved_path = p_preload->path->reduced_value;
		if (p_preload->resolved_path.is_relative_path()) {
			p_preload->resolved_path = parser->script_path.get_base_dir().path_join(p_preload->resolved_path);
		}
		p_preload->resolved_path = p_preload->resolved_path.simplify_path();
		const bool is_bootstrap_script_preload = !bootstrap_allowed_dependency_root.is_empty() &&
				p_preload->resolved_path.get_extension() == FSLanguage::get_singleton()->get_extension();
		if (is_bootstrap_script_preload) {
			if (!is_bootstrap_dependency_path_allowed(p_preload->resolved_path)) {
				push_error(vformat(R"(Build task bootstrap cannot preload script "%s"; it is outside the provider bootstrap root "%s".)",
								   p_preload->resolved_path, bootstrap_allowed_dependency_root),
						p_preload->path);
				return;
			}

			Error err = OK;
			Ref<FoundryScript> res = get_depended_shallow_script(p_preload->resolved_path, err);
			p_preload->resource = res;
			if (err != OK) {
				push_error(vformat(R"(Could not preload resource script "%s".)", p_preload->resolved_path), p_preload->path);
			} else {
				raise_preloaded_script_conformances();
			}
		} else if (!ResourceLoader::exists(p_preload->resolved_path)) {
			Ref<FileAccess> file_check = FileAccess::create(FileAccess::ACCESS_RESOURCES);

			if (file_check->file_exists(p_preload->resolved_path)) {
				push_error(vformat(R"(Preload file "%s" has no resource loaders (unrecognized file extension).)", p_preload->resolved_path), p_preload->path);
			} else {
				push_error(vformat(R"(Preload file "%s" does not exist.)", p_preload->resolved_path), p_preload->path);
			}
		} else {
			// TODO: Don't load if validating: use completion cache.

			// Must load FoundryScript separately to permit cyclic references
			// as ResourceLoader::load() detects and rejects those.
			const String &res_type = ResourceLoader::get_resource_type(p_preload->resolved_path);
			if (res_type == "FoundryScript") {
				Error err = OK;
				Ref<FoundryScript> res = get_depended_shallow_script(p_preload->resolved_path, err);
				p_preload->resource = res;
				if (err != OK) {
					push_error(vformat(R"(Could not preload resource script "%s".)", p_preload->resolved_path), p_preload->path);
				} else {
					// A retroactive conformance (`extend Target uses Trait: ...`) takes effect when its
					// declaring file is loaded by the using code, analogous to importing a module. When a
					// preloaded FS script declares conformances, eagerly raise it to `INTERFACE_SOLVED` so
					// its `resolve_conformances()` registers them before this file's body resolution
					// consults `is`/`as`/assignment against the externally-conformed types. Cyclic preloads
					// are safe: `raise_status()` advances the status before running each phase, so a
					// re-entrant raise to the same level returns without re-running it.
					raise_preloaded_script_conformances();
				}
			} else {
				Error err = OK;
				p_preload->resource = ResourceLoader::load(p_preload->resolved_path, res_type, ResourceFormatLoader::CACHE_MODE_REUSE, &err);
				if (err == ERR_BUSY) {
					p_preload->resource = ResourceLoader::ensure_resource_ref_override_for_outer_load(p_preload->resolved_path, res_type);
				}
				if (p_preload->resource.is_null()) {
					push_error(vformat(R"(Could not preload resource file "%s".)", p_preload->resolved_path), p_preload->path);
				}
			}
		}
	}

	finalize_preload();
}

void FSAnalyzer::reduce_self(FSParser::SelfNode *p_self) {
	p_self->is_constant = false;
	FSParser::DataType enum_type = enum_self_type(parser->current_function);
	if (enum_type.is_set()) {
		p_self->set_datatype(enum_type);
	} else if (parser->current_function != nullptr &&
			(parser->current_function->uses_receiver_relative_self ||
					_datatype_contains_self_type_parameter(parser->current_function->get_datatype()))) {
		p_self->set_datatype(_self_type_parameter_for_class(parser->current_class));
	} else {
		p_self->set_datatype(type_from_metatype(parser->current_class->get_datatype()));
	}
	mark_lambda_use_self();
}

// Handles `t.0`-style tuple index access. The parser only sets `is_tuple_index` when the index is a
// bare integer literal directly after a value token, so the index is always a constant here.
void FSAnalyzer::reduce_tuple_index_access(FSParser::SubscriptNode *p_subscript) {
	FSParser::DataType result_type;
	result_type.kind = FSParser::DataType::VARIANT;

	const FSParser::DataType base_type = p_subscript->base->get_datatype();
	const FSParser::DataType index_type = p_subscript->index->get_datatype();
	const bool has_constant_index = p_subscript->index->is_constant && p_subscript->index->reduced_value.get_type() == Variant::INT;

	if (base_type.kind == FSParser::DataType::TUPLE) {
		const int64_t index = has_constant_index ? p_subscript->index->reduced_value.operator int64_t() : 0;
		if (base_type.is_meta_type) {
			push_error(vformat(R"(Cannot index the tuple type "%s"; construct a value first.)", base_type.to_string()), p_subscript);
		} else if (!has_constant_index && index_type.is_hard_type() && !index_type.is_variant() &&
				!(index_type.kind == FSParser::DataType::BUILTIN && index_type.builtin_type == Variant::INT)) {
			push_error(vformat(R"(Only an integer can index tuple "%s", but received "%s".)", base_type.to_string(), index_type.to_string()),
					p_subscript->index);
		} else if (has_constant_index && (index < 0 || index >= base_type.container_element_types.size())) {
			push_error(vformat(R"(Tuple index %d is out of range for "%s", which has %d element(s).)",
							   index, base_type.to_string(), base_type.container_element_types.size()),
					p_subscript->index);
		} else if (!has_constant_index) {
			// A dynamic index cannot select an element type statically, since tuple elements are
			// heterogeneous.
			mark_node_unsafe(p_subscript);
		} else {
			result_type = base_type.get_container_element_type(index);
			result_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
			// A tuple element is only readable: writing through it is rejected in `reduce_assignment`,
			// and the runtime Array is read-only.
			result_type.is_read_only = true;
		}
	} else if (base_type.is_variant() || !base_type.is_hard_type()) {
		// A dynamic base lowers to a runtime indexed get.
		if (strict_dynamic_checks) {
			push_error("Cannot use tuple index access on Variant in strict dynamic mode.", p_subscript->base);
		} else {
			mark_node_unsafe(p_subscript);
		}
	} else {
		push_error(vformat(R"(Cannot use tuple index access on a value of type "%s".)", base_type.to_string()), p_subscript);
	}

	p_subscript->set_datatype(result_type);
}

// Handles `t.name` where the base is statically a tuple. Field names only exist statically, so an
// unknown name is an error rather than a dynamic property lookup.
void FSAnalyzer::reduce_tuple_field_access(FSParser::SubscriptNode *p_subscript, const FSParser::DataType &p_base_type) {
	FSParser::DataType result_type;
	result_type.kind = FSParser::DataType::VARIANT;

	if (p_base_type.is_meta_type) {
		push_error(vformat(R"(Cannot access member "%s" on the tuple type "%s"; construct a value first.)",
						   p_subscript->attribute->name, p_base_type.to_string()),
				p_subscript->attribute);
		p_subscript->set_datatype(result_type);
		return;
	}

	const int field_index = p_base_type.get_tuple_field_index(p_subscript->attribute->name);
	if (field_index < 0) {
		push_error(vformat(R"(Tuple "%s" has no field named "%s".)", p_base_type.to_string(), p_subscript->attribute->name),
				p_subscript->attribute);
		p_subscript->set_datatype(result_type);
		return;
	}

	result_type = p_base_type.get_container_element_type(field_index);
	result_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	result_type.is_read_only = true;
	p_subscript->attribute->set_datatype(result_type);
	p_subscript->set_datatype(result_type);
}

// Resolves p_name to a named-tuple declaration reachable from the call's base, so `Vec2(1, 2)` and
// `Outer.Vec2(1, 2)` both find the declaration. Returns the declaration's meta type.
bool FSAnalyzer::find_named_tuple_meta_type(const FSParser::DataType &p_base_type, bool p_is_self, const StringName &p_name,
		const FSParser::Node *p_source, FSParser::DataType &r_tuple_meta_type) {
	if (p_name == StringName()) {
		return false;
	}

	// A bare name is looked up in the enclosing class and then outwards through its lexical scopes;
	// a qualified name only looks at the named class itself.
	FSParser::ClassNode *candidate = p_is_self ? parser->current_class : nullptr;
	if (!p_is_self) {
		if (p_base_type.kind != FSParser::DataType::CLASS) {
			return false;
		}
		candidate = p_base_type.class_type;
	}

	while (candidate != nullptr) {
		if (candidate->has_member(p_name)) {
			if (candidate->get_member(p_name).type != FSParser::ClassNode::Member::TUPLE) {
				// A same-named member of another kind shadows any outer tuple declaration.
				return false;
			}
			resolve_class_member(candidate, p_name, p_source);
			const FSParser::DataType tuple_type = candidate->get_member(p_name).get_datatype();
			if (!tuple_type.is_set() || tuple_type.kind != FSParser::DataType::TUPLE) {
				return false;
			}
			r_tuple_meta_type = p_is_self ? tuple_type : substitute_member_type(tuple_type, p_base_type, nullptr, nullptr);
			return true;
		}
		candidate = p_is_self ? candidate->outer : nullptr;
	}
	return false;
}

// Resolves `Vec2(1, 2)` where `Vec2` is a whole-file tuple declared in another script, reached either
// by its global name or through an imported namespace. Any member of the current scope with that name
// shadows the global one, so an ordinary call is never rerouted into tuple construction.
bool FSAnalyzer::find_global_tuple_meta_type(const StringName &p_name, FSParser::Node *p_source,
		FSParser::DataType &r_tuple_meta_type) {
	if (p_name == StringName()) {
		return false;
	}

	List<FSParser::ClassNode *> scope_classes;
	get_class_node_current_scope_classes(parser->current_class, &scope_classes, p_source);
	for (FSParser::ClassNode *scope_class : scope_classes) {
		if (scope_class->members_indices.has(p_name) ||
				(scope_class->identifier != nullptr && scope_class->identifier->name == p_name)) {
			return false;
		}
	}

	// Precedence matches how a type annotation resolves a bare name: the file's own namespace first,
	// then imported namespaces, and only then an unnamespaced global of the same short name.
	StringName global_name;
	StringName namespace_candidate;
	bool namespace_error = false;
	if (get_global_class_in_namespace(parser->head->namespace_name, p_name, namespace_candidate)) {
		global_name = namespace_candidate;
	} else if (get_imported_global_class(p_name, p_source, namespace_candidate, namespace_error) && !namespace_error) {
		global_name = namespace_candidate;
	} else if (!namespace_error && ScriptServer::is_global_class(p_name)) {
		global_name = p_name;
	}
	if (global_name == StringName() || !ScriptServer::is_global_class(global_name)) {
		return false;
	}
	if (reject_bootstrap_global_class_dependency(global_name, p_source, "global type")) {
		return false;
	}

	const String path = ScriptServer::get_global_class_path(global_name);
	if (path.get_extension() != FSLanguage::get_singleton()->get_extension()) {
		return false;
	}

	// The declaring file is parsed before deciding, so a non-tuple global class leaves the call alone
	// instead of reporting an error from here.
	Ref<FSParserRef> ref;
	Error err = dependency_parser_access.raise_depended_parser_for(path, FSParserRef::INHERITANCE_SOLVED, ref);
	if (err != OK || ref.is_null() || ref->get_parser() == nullptr) {
		return false;
	}
	FSParser::ClassNode *global_head = ref->get_parser()->head;
	if (global_head == nullptr || !global_head->is_tuple_file) {
		return false;
	}

	r_tuple_meta_type = ref->get_analyzer()->make_global_tuple_type_from_current_parser(global_name, global_head);
	return r_tuple_meta_type.kind == FSParser::DataType::TUPLE;
}

// Checks `Vec2(a, b)` against the declaration: positional arguments only, exact arity, element-wise
// types. The result is the named tuple's instance type.
void FSAnalyzer::reduce_call_tuple_construction(FSParser::CallNode *p_call, const FSParser::DataType &p_tuple_meta_type) {
	call_site_validation.reject_named_call_arguments(p_call);
	p_call->is_tuple_construction = true;

	const FSParser::DataType tuple_type = type_from_metatype(p_tuple_meta_type);
	const int expected_count = tuple_type.container_element_types.size();
	if (p_call->arguments.size() != expected_count) {
		push_error(vformat(R"*(Tuple "%s" expects %d argument(s), but %d were given.)*",
						   tuple_type.to_string(), expected_count, p_call->arguments.size()),
				p_call);
		p_call->set_datatype(tuple_type);
		return;
	}

	for (int i = 0; i < expected_count; i++) {
		const FSParser::DataType field_type = tuple_type.get_container_element_type(i);
		FSParser::ExpressionNode *argument = p_call->arguments[i];
		const FSParser::DataType argument_type = argument->get_datatype();
		if (!argument_type.is_set()) {
			continue;
		}
		if (!is_type_compatible(field_type, argument_type, true)) {
			push_error(vformat(R"*(Invalid argument %d for tuple "%s": should be "%s" but is "%s".)*",
							   i + 1, tuple_type.to_string(), field_type.to_string(), argument_type.to_string()),
					argument);
			continue;
		}
		if (argument->is_constant) {
			// Widens a constant to the declared field type (e.g. an int literal into a float field).
			update_const_expression_builtin_type(argument, field_type, "pass");
		} else if (!field_type.is_variant() && (argument_type.is_variant() || !argument_type.is_hard_type())) {
			mark_node_unsafe(p_call);
		}
	}

	p_call->set_datatype(tuple_type);
}

void FSAnalyzer::reduce_call_enum_case_construction(FSParser::CallNode *p_call, const FSParser::DataType &p_enum_meta_type) {
	call_site_validation.reject_named_call_arguments(p_call);

	const StringName case_name = p_call->function_name;
	const FSParser::DataType::EnumCasePayload *payload = p_enum_meta_type.get_enum_case_payload(case_name);
	ERR_FAIL_NULL(payload);

	const FSParser::DataType case_value_type = type_from_metatype(p_enum_meta_type);
	const int64_t *tag = p_enum_meta_type.enum_values.getptr(case_name);
	p_call->is_enum_case_construction = true;
	p_call->enum_case_tag = tag != nullptr ? *tag : 0;

	const int expected_count = payload->field_types.size();
	if (p_call->arguments.size() != expected_count) {
		push_error(vformat(R"*(Enum case "%s.%s" expects %d argument(s), but %d were given.)*",
						   p_enum_meta_type.enum_type, case_name, expected_count, p_call->arguments.size()),
				p_call);
		p_call->set_datatype(case_value_type);
		return;
	}

	for (int i = 0; i < expected_count; i++) {
		const FSParser::DataType field_type = payload->field_types[i];
		FSParser::ExpressionNode *argument = p_call->arguments[i];
		const FSParser::DataType argument_type = argument->get_datatype();
		if (!argument_type.is_set()) {
			continue;
		}
		if (!is_type_compatible(field_type, argument_type, true)) {
			push_error(vformat(R"*(Invalid argument %d for enum case "%s.%s": should be "%s" but is "%s".)*",
							   i + 1, p_enum_meta_type.enum_type, case_name, field_type.to_string(), argument_type.to_string()),
					argument);
			continue;
		}
		if (argument->is_constant) {
			// Widens a constant to the declared field type (e.g. an int literal into a float field).
			update_const_expression_builtin_type(argument, field_type, "pass");
		} else if (!field_type.is_variant() && (argument_type.is_variant() || !argument_type.is_hard_type())) {
			mark_node_unsafe(p_call);
		}
	}

	p_call->set_datatype(case_value_type);
}

void FSAnalyzer::reduce_subscript(FSParser::SubscriptNode *p_subscript, bool p_can_be_pseudo_type) {
	if (p_subscript->base == nullptr) {
		return;
	}
	if (p_subscript->is_attribute && p_subscript->attribute != nullptr) {
		Vector<FSParser::IdentifierNode *> reversed_chain;
		FSParser::ExpressionNode *chain_base = p_subscript;
		while (chain_base != nullptr && chain_base->type == FSParser::Node::SUBSCRIPT) {
			FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(chain_base);
			if (!subscript->is_attribute || subscript->attribute == nullptr) {
				break;
			}
			reversed_chain.push_back(subscript->attribute);
			chain_base = subscript->base;
		}

		if (chain_base != nullptr && chain_base->type == FSParser::Node::IDENTIFIER) {
			FSParser::IdentifierNode *root_identifier = static_cast<FSParser::IdentifierNode *>(chain_base);
			if (root_identifier->source == FSParser::IdentifierNode::UNDEFINED_SOURCE && !is_namespace_chain_root_shadowed(root_identifier)) {
				Vector<FSParser::IdentifierNode *> type_chain;
				type_chain.push_back(root_identifier);
				for (int i = reversed_chain.size() - 1; i >= 0; i--) {
					type_chain.push_back(reversed_chain[i]);
				}

				StringName namespace_global_class;
				bool namespace_error = false;
				int namespace_type_chain_size = 0;
				if (get_namespace_global_class_from_type_chain(type_chain, p_subscript, namespace_global_class, namespace_type_chain_size, namespace_error)) {
					if (namespace_error) {
						FSParser::DataType dummy;
						dummy.kind = FSParser::DataType::VARIANT;
						p_subscript->set_datatype(dummy);
						return;
					}

					// Mark the sub-expression that spans exactly the resolved global class
					// (`ns.Foo`, or just `Foo` for a same-namespace/imported prefix) so
					// compiler lowering does not fall through to a namespace-root identifier.
					// The class is the leading `namespace_type_chain_size` identifiers; any
					// remaining identifiers are member access on that class.
					FSParser::ExpressionNode *class_prefix = p_subscript;
					for (int i = 0; i < type_chain.size() - namespace_type_chain_size; i++) {
						if (class_prefix->type != FSParser::Node::SUBSCRIPT) {
							class_prefix = nullptr;
							break;
						}
						class_prefix = static_cast<FSParser::SubscriptNode *>(class_prefix)->base;
					}

					FSParser::DataType namespace_class_type;
					const bool namespace_class_is_enum = ScriptServer::is_global_class_enum(namespace_global_class);
					if (namespace_class_is_enum) {
						const String path = ScriptServer::get_global_class_path(namespace_global_class);
						namespace_class_type = make_global_enum_type_from_path(namespace_global_class, path, p_subscript);
					} else {
						namespace_class_type = make_global_class_meta_type(namespace_global_class, p_subscript);
					}
					const FSParser::DataType resolved_namespace_class_type = namespace_class_type;
					for (int i = namespace_type_chain_size; i < type_chain.size(); i++) {
						FSParser::DataType base = namespace_class_type;
						reduce_identifier_from_base(type_chain[i], &base);
						namespace_class_type = type_chain[i]->get_datatype();
						if (!namespace_class_type.is_set()) {
							FSParser::DataType dummy;
							dummy.kind = FSParser::DataType::VARIANT;
							p_subscript->set_datatype(dummy);
							return;
						}
					}
					FSParser::IdentifierNode *last_identifier = type_chain[type_chain.size() - 1];
					p_subscript->attribute->set_datatype(namespace_class_type);
					p_subscript->set_datatype(namespace_class_type);
					p_subscript->is_constant = last_identifier->is_constant;
					p_subscript->reduced_value = last_identifier->reduced_value;
					if (class_prefix != nullptr) {
						if (namespace_class_is_enum) {
							// Apply this after propagating the final chain member above. For an
							// exact enum prefix (`ns.Status`), that propagation otherwise copies
							// the unresolved `Status` leaf back over the materialized constant.
							class_prefix->set_datatype(resolved_namespace_class_type);
							class_prefix->is_constant = true;
							class_prefix->reduced_value = make_enum_dictionary_from_type(resolved_namespace_class_type);
						} else {
							class_prefix->resolved_global_class = namespace_global_class;
						}
					}
					return;
				}
			}
		}
	}
	if (p_subscript->base->type == FSParser::Node::IDENTIFIER) {
		reduce_identifier(static_cast<FSParser::IdentifierNode *>(p_subscript->base), true);
	} else if (p_subscript->base->type == FSParser::Node::SUBSCRIPT) {
		reduce_subscript(static_cast<FSParser::SubscriptNode *>(p_subscript->base), true);
	} else {
		reduce_expression(p_subscript->base);
	}

	FSParser::DataType result_type;

	if (p_subscript->is_attribute) {
		if (p_subscript->attribute == nullptr) {
			return;
		}

		FSParser::DataType base_type = p_subscript->base->get_datatype();
		bool valid = false;

		if (base_type.is_set() && base_type.kind == FSParser::DataType::TUPLE) {
			reduce_tuple_field_access(p_subscript, base_type);
			return;
		}

		// If the base is a metatype, use the analyzer instead.
		if (p_subscript->base->is_constant && !base_type.is_meta_type) {
			// GH-92534. If the base is a FoundryScript, use the analyzer instead.
			bool base_is_foundry_script = false;
			if (p_subscript->base->reduced_value.get_type() == Variant::OBJECT) {
				Ref<FoundryScript> foundry_script = Object::cast_to<FoundryScript>(p_subscript->base->reduced_value.get_validated_object());
				if (foundry_script.is_valid()) {
					base_is_foundry_script = true;
					// Makes a metatype from a constant FoundryScript, since `base_type` is not a metatype.
					FSParser::DataType base_type_meta = type_from_variant(foundry_script, p_subscript);
					// First try to reduce the attribute from the metatype.
					reduce_identifier_from_base(p_subscript->attribute, &base_type_meta);
					FSParser::DataType attr_type = p_subscript->attribute->get_datatype();
					if (attr_type.is_set()) {
						valid = !attr_type.is_pseudo_type || p_can_be_pseudo_type;
						result_type = attr_type;
						p_subscript->is_constant = p_subscript->attribute->is_constant;
						p_subscript->reduced_value = p_subscript->attribute->reduced_value;
					}
					if (!valid) {
						// If unsuccessful, reset and return to the normal route.
						p_subscript->attribute->set_datatype(FSParser::DataType());
					}
				}
			}
			if (!base_is_foundry_script) {
				// Just try to get it.
				Variant value = p_subscript->base->reduced_value.get_named(p_subscript->attribute->name, valid);
				if (valid) {
					p_subscript->is_constant = true;
					p_subscript->reduced_value = value;
					result_type = type_from_variant(value, p_subscript);
				}
			}
		}

		if (valid) {
			// Do nothing.
		} else if (base_type.is_variant() || !base_type.is_hard_type()) {
			valid = !base_type.is_pseudo_type || p_can_be_pseudo_type;
			result_type.kind = FSParser::DataType::VARIANT;
			if (base_type.is_variant() && base_type.is_hard_type() && base_type.is_meta_type && base_type.is_pseudo_type) {
				// Special case: it may be a global enum with pseudo base (e.g. Variant.Type).
				String enum_name;
				if (p_subscript->base->type == FSParser::Node::IDENTIFIER) {
					enum_name = String(static_cast<FSParser::IdentifierNode *>(p_subscript->base)->name) + ENUM_SEPARATOR + String(p_subscript->attribute->name);
				}
				if (CoreConstants::is_global_enum(enum_name)) {
					result_type = make_global_enum_type(enum_name, StringName());
				} else {
					valid = false;
					mark_node_unsafe(p_subscript);
				}
			} else if (strict_dynamic_checks) {
				push_error(vformat(R"*(Cannot resolve member "%s" on type "%s" in strict dynamic mode.)*", p_subscript->attribute->name, base_type.to_string()), p_subscript->attribute);
			} else {
				mark_node_unsafe(p_subscript);
			}
		} else {
			reduce_identifier_from_base(p_subscript->attribute, &base_type);
			FSParser::DataType attr_type = p_subscript->attribute->get_datatype();
			if (attr_type.is_set()) {
				if (base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_types()) {
					Variant::Type key_type = base_type.get_container_element_type_or_variant(0).builtin_type;
					valid = key_type == Variant::NIL || key_type == Variant::STRING || key_type == Variant::STRING_NAME;
					if (base_type.has_container_element_type(1)) {
						result_type = base_type.get_container_element_type(1);
						result_type.type_source = base_type.type_source;
					} else {
						result_type.builtin_type = Variant::NIL;
						result_type.kind = FSParser::DataType::VARIANT;
						result_type.type_source = FSParser::DataType::UNDETECTED;
					}
				} else {
					valid = !attr_type.is_pseudo_type || p_can_be_pseudo_type;
					result_type = attr_type;
					p_subscript->is_constant = p_subscript->attribute->is_constant;
					p_subscript->reduced_value = p_subscript->attribute->reduced_value;
				}
			} else if (!base_type.is_meta_type || !base_type.is_constant) {
				valid = base_type.kind != FSParser::DataType::BUILTIN;
				if (valid) {
					if (strict_dynamic_checks) {
						push_error(vformat(R"*(Cannot resolve member "%s" on type "%s" in strict dynamic mode.)*", p_subscript->attribute->name, base_type.to_string()), p_subscript->attribute);
					} else {
#ifdef DEBUG_ENABLED
						parser->push_warning(p_subscript, FSWarning::UNSAFE_PROPERTY_ACCESS, p_subscript->attribute->name, base_type.to_string());
#endif // DEBUG_ENABLED
					}
				}
				result_type.kind = FSParser::DataType::VARIANT;
				mark_node_unsafe(p_subscript);
			}
		}

		if (!valid) {
			FSParser::DataType attr_type = p_subscript->attribute->get_datatype();
			if (!p_can_be_pseudo_type && (attr_type.is_pseudo_type || result_type.is_pseudo_type)) {
				push_error(vformat(R"(Type "%s" in base "%s" cannot be used on its own.)", p_subscript->attribute->name, type_from_metatype(base_type).to_string()), p_subscript->attribute);
			} else {
				push_error(vformat(R"(Cannot find member "%s" in base "%s".)", p_subscript->attribute->name, type_from_metatype(base_type).to_string()), p_subscript->attribute);
			}
			result_type.kind = FSParser::DataType::VARIANT;
		}
	} else {
		if (p_subscript->index == nullptr) {
			return;
		}

		FSParser::DataType base_meta_type = p_subscript->base->get_datatype();
		if (base_meta_type.is_set() && base_meta_type.is_meta_type && base_meta_type.kind == FSParser::DataType::CLASS &&
				base_meta_type.class_type != nullptr && !base_meta_type.class_type->type_parameters.is_empty()) {
			// Generic class specialization in value position, e.g. `Box[int]` or `Pair[int, String]`.
			// The brackets carry a type-argument list rather than an index. A multi-argument list is
			// captured in `type_arguments`; a single argument keeps using `index`.
			FSParser::DataType specialized = base_meta_type;
			Vector<FSParser::DataType> resolved_arguments;
			Vector<bool> argument_failed;
			Vector<const FSParser::Node *> argument_sources;
			Vector<FSParser::ExpressionNode *> argument_expressions;
			if (p_subscript->type_arguments.is_empty()) {
				argument_expressions.push_back(p_subscript->index);
			} else {
				argument_expressions = p_subscript->type_arguments;
			}
			for (FSParser::ExpressionNode *argument_expression : argument_expressions) {
				// Resolve positionally: a failed argument keeps its slot (filled with the Variant
				// fallback and flagged) so the arity check sees the count the user wrote and a later
				// argument is never shifted into an earlier type parameter.
				FSParser::DataType type_argument;
				if (resolve_explicit_type_argument(argument_expression, type_argument)) {
					resolved_arguments.push_back(type_argument);
					argument_failed.push_back(false);
				} else {
					push_error(vformat(R"(Could not resolve the type argument for generic class "%s".)", specialized.to_string()), argument_expression);
					FSParser::DataType fallback;
					fallback.kind = FSParser::DataType::VARIANT;
					fallback.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
					resolved_arguments.push_back(fallback);
					argument_failed.push_back(true);
				}
				argument_sources.push_back(argument_expression);
			}

			const int expected_argument_count = specialized.class_type->type_parameters.size();
			if (resolved_arguments.size() != expected_argument_count) {
				push_error(vformat(R"(Generic class "%s" expects %d type argument(s), but %d were given.)", specialized.to_string(), expected_argument_count, resolved_arguments.size()), p_subscript);
				result_type.kind = FSParser::DataType::VARIANT;
			} else {
				bind_class_type_arguments(specialized, resolved_arguments, argument_failed, argument_sources, p_subscript);
				specialized.is_meta_type = true;
				result_type = specialized;
				// The specialized handle still refers to the same class object at runtime; carry the
				// base's constant value so `Box[int].new()` can recover the script to instantiate.
				p_subscript->is_constant = p_subscript->base->is_constant;
				p_subscript->reduced_value = p_subscript->base->reduced_value;
			}
			p_subscript->set_datatype(result_type);
			return;
		}

		if (!p_subscript->type_arguments.is_empty()) {
			// A comma-separated or `?`-marked type-argument list only makes sense as a generic
			// specialization (handled above) or an explicit generic-method application (handled in
			// call reduction). Reaching here means it was used as an ordinary subscript, which never
			// accepts more than one index.
			push_error(R"(Only a single index is allowed in the subscript operator.)", p_subscript);
			result_type.kind = FSParser::DataType::VARIANT;
			p_subscript->set_datatype(result_type);
			return;
		}

		reduce_expression(p_subscript->index);

		if (p_subscript->is_tuple_index || p_subscript->base->get_datatype().kind == FSParser::DataType::TUPLE) {
			reduce_tuple_index_access(p_subscript);
			return;
		}

		if (p_subscript->base->is_constant && p_subscript->index->is_constant) {
			// Just try to get it.
			bool valid = false;
			// TODO: Check if `p_subscript->base->reduced_value` is FoundryScript.
			Variant value = p_subscript->base->reduced_value.get(p_subscript->index->reduced_value, &valid);
			if (!valid) {
				push_error(vformat(R"(Cannot get index "%s" from "%s".)", p_subscript->index->reduced_value, p_subscript->base->reduced_value), p_subscript->index);
				result_type.kind = FSParser::DataType::VARIANT;
			} else {
				p_subscript->is_constant = true;
				p_subscript->reduced_value = value;
				result_type = type_from_variant(value, p_subscript);
			}
		} else {
			FSParser::DataType base_type = p_subscript->base->get_datatype();
			FSParser::DataType index_type = p_subscript->index->get_datatype();

			if (base_type.is_variant()) {
				result_type.kind = FSParser::DataType::VARIANT;
				if (strict_dynamic_checks) {
					push_error("Cannot use subscript operator on Variant in strict dynamic mode.", p_subscript->base);
				} else {
					mark_node_unsafe(p_subscript);
				}
			} else {
				if (index_type.is_variant() && strict_dynamic_checks) {
					push_error(vformat(R"*(Cannot use dynamic index of type "%s" for base of type "%s" in strict dynamic mode.)*", index_type.to_string(), base_type.to_string()), p_subscript->index);
				}
				if (base_type.kind == FSParser::DataType::BUILTIN && !index_type.is_variant()) {
					// Check if indexing is valid.
					bool error = index_type.kind != FSParser::DataType::BUILTIN && base_type.builtin_type != Variant::DICTIONARY;
					if (!error) {
						switch (base_type.builtin_type) {
							// Expect int or real as index.
							case Variant::PACKED_BYTE_ARRAY:
							case Variant::PACKED_FLOAT32_ARRAY:
							case Variant::PACKED_FLOAT64_ARRAY:
							case Variant::PACKED_INT32_ARRAY:
							case Variant::PACKED_INT64_ARRAY:
							case Variant::PACKED_STRING_ARRAY:
							case Variant::PACKED_VECTOR2_ARRAY:
							case Variant::PACKED_VECTOR3_ARRAY:
							case Variant::PACKED_COLOR_ARRAY:
							case Variant::PACKED_VECTOR4_ARRAY:
							case Variant::ARRAY:
							case Variant::STRING:
								error = index_type.builtin_type != Variant::INT && index_type.builtin_type != Variant::FLOAT;
								break;
							// Expect String only.
							case Variant::RECT2:
							case Variant::RECT2I:
							case Variant::PLANE:
							case Variant::QUATERNION:
							case Variant::AABB:
							case Variant::OBJECT:
								error = index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME;
								break;
							// Expect String or number.
							case Variant::BASIS:
							case Variant::VECTOR2:
							case Variant::VECTOR2I:
							case Variant::VECTOR3:
							case Variant::VECTOR3I:
							case Variant::VECTOR4:
							case Variant::VECTOR4I:
							case Variant::TRANSFORM2D:
							case Variant::TRANSFORM3D:
							case Variant::PROJECTION:
								error = index_type.builtin_type != Variant::INT && index_type.builtin_type != Variant::FLOAT &&
										index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME;
								break;
							// Expect String or int.
							case Variant::COLOR:
								error = index_type.builtin_type != Variant::INT && index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME;
								break;
							// Don't support indexing, but we will check it later.
							case Variant::RID:
							case Variant::BOOL:
							case Variant::CALLABLE:
							case Variant::FLOAT:
							case Variant::INT:
							case Variant::NIL:
							case Variant::NODE_PATH:
							case Variant::SIGNAL:
							case Variant::STRING_NAME:
								break;
							// Support depends on if the dictionary has a typed key, otherwise anything is valid.
							case Variant::DICTIONARY:
								if (base_type.has_container_element_type(0)) {
									FSParser::DataType key_type = base_type.get_container_element_type(0);
									// A `Dictionary[K, V]` whose key type (transitively) involves a method/class type
									// parameter is erased to an untyped key at runtime, so there is no key metadata to
									// validate statically; indexing it inside a generic body is allowed. Mirrors how
									// `Array[T]` element parameters were erased. The key type must itself be erased — a
									// concretely-keyed dictionary (`Dictionary[int, String]`) indexed by an unrelated
									// type parameter is still checked below.
									if (_signature_type_involves_type_parameter(key_type)) {
										break;
									}
									switch (index_type.builtin_type) {
										// Null value will be treated as an empty object, allow.
										case Variant::NIL:
											error = key_type.builtin_type != Variant::OBJECT;
											break;
										// Objects are parsed for validity in a similar manner to container types.
										case Variant::OBJECT:
											if (key_type.builtin_type == Variant::OBJECT) {
												error = !key_type.can_reference(index_type);
											} else {
												error = key_type.builtin_type != Variant::NIL;
											}
											break;
										// String and StringName interchangeable in this context.
										case Variant::STRING:
										case Variant::STRING_NAME:
											error = key_type.builtin_type != Variant::STRING_NAME && key_type.builtin_type != Variant::STRING;
											break;
										// Ints are valid indices for floats, but not the other way around.
										case Variant::INT:
											error = key_type.builtin_type != Variant::INT && key_type.builtin_type != Variant::FLOAT;
											break;
										// All other cases require the types to match exactly.
										default:
											error = key_type.builtin_type != index_type.builtin_type;
											break;
									}
								}
								break;
							// Here for completeness.
							case Variant::VARIANT_MAX:
								break;
						}

						if (error) {
							push_error(vformat(R"(Invalid index type "%s" for a base of type "%s".)", index_type.to_string(), base_type.to_string()), p_subscript->index);
						}
					}
				} else if (base_type.kind != FSParser::DataType::BUILTIN && !index_type.is_variant()) {
					if (index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME) {
						push_error(vformat(R"(Only "String" or "StringName" can be used as index for type "%s", but received "%s".)", base_type.to_string(), index_type.to_string()), p_subscript->index);
					}
				}

				// Check resulting type if possible.
				result_type.builtin_type = Variant::NIL;
				result_type.kind = FSParser::DataType::BUILTIN;
				result_type.type_source = base_type.is_hard_type() ? FSParser::DataType::ANNOTATED_INFERRED : FSParser::DataType::INFERRED;

				if (base_type.kind != FSParser::DataType::BUILTIN) {
					base_type.builtin_type = Variant::OBJECT;
				}
				switch (base_type.builtin_type) {
					// Can't index at all.
					case Variant::RID:
					case Variant::BOOL:
					case Variant::CALLABLE:
					case Variant::FLOAT:
					case Variant::INT:
					case Variant::NIL:
					case Variant::NODE_PATH:
					case Variant::SIGNAL:
					case Variant::STRING_NAME:
						result_type.kind = FSParser::DataType::VARIANT;
						push_error(vformat(R"(Cannot use subscript operator on a base of type "%s".)", base_type.to_string()), p_subscript->base);
						break;
					// Return int.
					case Variant::PACKED_BYTE_ARRAY:
					case Variant::PACKED_INT32_ARRAY:
					case Variant::PACKED_INT64_ARRAY:
					case Variant::VECTOR2I:
					case Variant::VECTOR3I:
					case Variant::VECTOR4I:
						result_type.builtin_type = Variant::INT;
						break;
					// Return float.
					case Variant::PACKED_FLOAT32_ARRAY:
					case Variant::PACKED_FLOAT64_ARRAY:
					case Variant::VECTOR2:
					case Variant::VECTOR3:
					case Variant::VECTOR4:
					case Variant::QUATERNION:
						result_type.builtin_type = Variant::FLOAT;
						break;
					// Return String.
					case Variant::PACKED_STRING_ARRAY:
					case Variant::STRING:
						result_type.builtin_type = Variant::STRING;
						break;
					// Return Vector2.
					case Variant::PACKED_VECTOR2_ARRAY:
					case Variant::TRANSFORM2D:
					case Variant::RECT2:
						result_type.builtin_type = Variant::VECTOR2;
						break;
					// Return Vector2I.
					case Variant::RECT2I:
						result_type.builtin_type = Variant::VECTOR2I;
						break;
					// Return Vector3.
					case Variant::PACKED_VECTOR3_ARRAY:
					case Variant::AABB:
					case Variant::BASIS:
						result_type.builtin_type = Variant::VECTOR3;
						break;
					// Return Color.
					case Variant::PACKED_COLOR_ARRAY:
						result_type.builtin_type = Variant::COLOR;
						break;
					// Return Vector4.
					case Variant::PACKED_VECTOR4_ARRAY:
						result_type.builtin_type = Variant::VECTOR4;
						break;
					// Depends on the index.
					case Variant::TRANSFORM3D:
					case Variant::PROJECTION:
					case Variant::PLANE:
					case Variant::COLOR:
					case Variant::OBJECT:
						result_type.kind = FSParser::DataType::VARIANT;
						result_type.type_source = FSParser::DataType::UNDETECTED;
						break;
					// Can have an element type.
					case Variant::ARRAY:
						if (base_type.has_container_element_type(0)) {
							result_type = base_type.get_container_element_type(0);
							result_type.type_source = base_type.type_source;
						} else {
							result_type.kind = FSParser::DataType::VARIANT;
							result_type.type_source = FSParser::DataType::UNDETECTED;
						}
						break;
					// Can have two element types, but we only care about the value.
					case Variant::DICTIONARY:
						if (base_type.has_container_element_type(1)) {
							result_type = base_type.get_container_element_type(1);
							result_type.type_source = base_type.type_source;
						} else {
							result_type.kind = FSParser::DataType::VARIANT;
							result_type.type_source = FSParser::DataType::UNDETECTED;
						}
						break;
					// Here for completeness.
					case Variant::VARIANT_MAX:
						break;
				}
			}
		}
	}

	p_subscript->set_datatype(result_type);
}

void FSAnalyzer::reduce_ternary_op(FSParser::TernaryOpNode *p_ternary_op, bool p_is_root) {
	reduce_expression(p_ternary_op->condition);
	reduce_expression(p_ternary_op->true_expr, p_is_root);
	reduce_expression(p_ternary_op->false_expr, p_is_root);

	FSParser::DataType result;

	if (p_ternary_op->condition && p_ternary_op->condition->is_constant && p_ternary_op->true_expr->is_constant && p_ternary_op->false_expr && p_ternary_op->false_expr->is_constant) {
		p_ternary_op->is_constant = true;
		if (p_ternary_op->condition->reduced_value.booleanize()) {
			p_ternary_op->reduced_value = p_ternary_op->true_expr->reduced_value;
		} else {
			p_ternary_op->reduced_value = p_ternary_op->false_expr->reduced_value;
		}
	}

	FSParser::DataType true_type;
	if (p_ternary_op->true_expr) {
		true_type = p_ternary_op->true_expr->get_datatype();
	} else {
		true_type.kind = FSParser::DataType::VARIANT;
	}
	FSParser::DataType false_type;
	if (p_ternary_op->false_expr) {
		false_type = p_ternary_op->false_expr->get_datatype();
	} else {
		false_type.kind = FSParser::DataType::VARIANT;
	}

	const bool true_is_null = true_type.kind == FSParser::DataType::BUILTIN && true_type.builtin_type == Variant::NIL;
	const bool false_is_null = false_type.kind == FSParser::DataType::BUILTIN && false_type.builtin_type == Variant::NIL;

	if (true_is_null != false_is_null) {
		result = true_is_null ? false_type : true_type;
		result.is_nullable = true;
	} else if (true_type.is_variant() || false_type.is_variant()) {
		result.kind = FSParser::DataType::VARIANT;
	} else {
		result = true_type;
		if (!is_type_compatible(true_type, false_type)) {
			result = false_type;
			if (!is_type_compatible(false_type, true_type)) {
				result.kind = FSParser::DataType::VARIANT;
#ifdef DEBUG_ENABLED
				parser->push_warning(p_ternary_op, FSWarning::INCOMPATIBLE_TERNARY);
#endif // DEBUG_ENABLED
			}
		}
	}
	result.type_source = true_type.is_hard_type() && false_type.is_hard_type() ? FSParser::DataType::ANNOTATED_INFERRED : FSParser::DataType::INFERRED;

	p_ternary_op->set_datatype(result);
}

void FSAnalyzer::reduce_type_test(FSParser::TypeTestNode *p_type_test) {
	FSParser::DataType result;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	result.kind = FSParser::DataType::BUILTIN;
	result.builtin_type = Variant::BOOL;
	p_type_test->set_datatype(result);

	if (!p_type_test->operand || !p_type_test->test_type) {
		return;
	}

	reduce_expression(p_type_test->operand);
	FSParser::DataType operand_type = p_type_test->operand->get_datatype();
	FSParser::DataType test_type = type_from_metatype(resolve_datatype(p_type_test->test_type));
	p_type_test->test_datatype = test_type;

	if (!operand_type.is_set() || !test_type.is_set()) {
		return;
	}

	if (p_type_test->operand->is_constant) {
		p_type_test->is_constant = true;
		p_type_test->reduced_value = false;

		if (!is_type_compatible(test_type, operand_type)) {
			push_error(vformat(R"(Expression is of type "%s" so it can't be of type "%s".)", operand_type.to_string(), test_type.to_string()), p_type_test->operand);
		} else {
			// The constant's type can still be resolving during re-entrant member resolution;
			// only fold the test when it is known (`is_type_compatible()` treats unset as compatible anyway).
			const FSParser::DataType value_type = type_from_variant(p_type_test->operand->reduced_value, p_type_test->operand);
			if (value_type.is_set() && is_type_compatible(test_type, value_type)) {
				p_type_test->reduced_value = test_type.builtin_type != Variant::OBJECT || !p_type_test->operand->reduced_value.is_null();
			}
		}

		return;
	}

	if (!is_type_compatible(test_type, operand_type) && !is_type_compatible(operand_type, test_type)) {
		if (operand_type.is_hard_type()) {
			push_error(vformat(R"(Expression is of type "%s" so it can't be of type "%s".)", operand_type.to_string(), test_type.to_string()), p_type_test->operand);
		} else {
			downgrade_node_type_source(p_type_test->operand);
		}
	}
}

void FSAnalyzer::reduce_unary_op(FSParser::UnaryOpNode *p_unary_op) {
	reduce_expression(p_unary_op->operand);

	FSParser::DataType result;

	if (p_unary_op->operand == nullptr) {
		result.kind = FSParser::DataType::VARIANT;
		p_unary_op->set_datatype(result);
		return;
	}

	FSParser::DataType operand_type = p_unary_op->operand->get_datatype();

	if (p_unary_op->operand->is_constant) {
		p_unary_op->is_constant = true;
		p_unary_op->reduced_value = Variant::evaluate(p_unary_op->variant_op, p_unary_op->operand->reduced_value, Variant());
		result = type_from_variant(p_unary_op->reduced_value, p_unary_op);
	}

	if (operand_type.is_variant()) {
		result.kind = FSParser::DataType::VARIANT;
		if (strict_dynamic_checks) {
			push_error(vformat(R"*(Cannot use dynamic operand for unary "%s" operator in strict dynamic mode.)*", Variant::get_operator_name(p_unary_op->variant_op)), p_unary_op);
		} else {
			mark_node_unsafe(p_unary_op);
		}
	} else {
		bool valid = false;
		result = get_operation_type(p_unary_op->variant_op, operand_type, valid, p_unary_op);

		if (!valid) {
			push_error(vformat(R"(Invalid operand of type "%s" for unary operator "%s".)", operand_type.to_string(), Variant::get_operator_name(p_unary_op->variant_op)), p_unary_op);
		}
	}

	p_unary_op->set_datatype(result);
}

Variant FSAnalyzer::make_expression_reduced_value(FSParser::ExpressionNode *p_expression, bool &is_reduced) {
	if (p_expression == nullptr) {
		return Variant();
	}

	if (p_expression->is_constant) {
		is_reduced = true;
		return p_expression->reduced_value;
	}

	switch (p_expression->type) {
		case FSParser::Node::ARRAY:
			return make_array_reduced_value(static_cast<FSParser::ArrayNode *>(p_expression), is_reduced);
		case FSParser::Node::TUPLE_LITERAL:
			return make_tuple_literal_reduced_value(static_cast<FSParser::TupleLiteralNode *>(p_expression), is_reduced);
		case FSParser::Node::DICTIONARY:
			return make_dictionary_reduced_value(static_cast<FSParser::DictionaryNode *>(p_expression), is_reduced);
		case FSParser::Node::SUBSCRIPT:
			return make_subscript_reduced_value(static_cast<FSParser::SubscriptNode *>(p_expression), is_reduced);
		case FSParser::Node::CALL:
			return make_call_reduced_value(static_cast<FSParser::CallNode *>(p_expression), is_reduced);
		default:
			break;
	}

	return Variant();
}

Variant FSAnalyzer::make_array_reduced_value(FSParser::ArrayNode *p_array, bool &is_reduced) {
	Array array = p_array->get_datatype().has_container_element_type(0) ? make_array_from_element_datatype(p_array->get_datatype().get_container_element_type(0)) : Array();

	array.resize(p_array->elements.size());
	for (int i = 0; i < p_array->elements.size(); i++) {
		FSParser::ExpressionNode *element = p_array->elements[i];

		bool is_element_value_reduced = false;
		Variant element_value = make_expression_reduced_value(element, is_element_value_reduced);
		if (!is_element_value_reduced) {
			return Variant();
		}

		array[i] = element_value;
	}

	array.make_read_only();

	is_reduced = true;
	return array;
}

Variant FSAnalyzer::make_tuple_literal_reduced_value(FSParser::TupleLiteralNode *p_tuple_literal, bool &is_reduced) {
	// Tuple values erase to a plain (read-only) Array at runtime, matching how a tuple
	// literal is compiled; see the design doc. Static tuple typing is a follow-up change.
	Array array;
	array.resize(p_tuple_literal->elements.size());
	for (int i = 0; i < p_tuple_literal->elements.size(); i++) {
		FSParser::ExpressionNode *element = p_tuple_literal->elements[i];

		bool is_element_value_reduced = false;
		Variant element_value = make_expression_reduced_value(element, is_element_value_reduced);
		if (!is_element_value_reduced) {
			return Variant();
		}

		array[i] = element_value;
	}

	array.make_read_only();

	is_reduced = true;
	return array;
}

Variant FSAnalyzer::make_dictionary_reduced_value(FSParser::DictionaryNode *p_dictionary, bool &is_reduced) {
	Dictionary dictionary = p_dictionary->get_datatype().has_container_element_types()
			? make_dictionary_from_element_datatype(p_dictionary->get_datatype().get_container_element_type_or_variant(0), p_dictionary->get_datatype().get_container_element_type_or_variant(1))
			: Dictionary();

	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		const FSParser::DictionaryNode::Pair &element = p_dictionary->elements[i];

		bool is_element_key_reduced = false;
		Variant element_key = make_expression_reduced_value(element.key, is_element_key_reduced);
		if (!is_element_key_reduced) {
			return Variant();
		}

		bool is_element_value_reduced = false;
		Variant element_value = make_expression_reduced_value(element.value, is_element_value_reduced);
		if (!is_element_value_reduced) {
			return Variant();
		}

		dictionary[element_key] = element_value;
	}

	dictionary.make_read_only();

	is_reduced = true;
	return dictionary;
}

Variant FSAnalyzer::make_subscript_reduced_value(FSParser::SubscriptNode *p_subscript, bool &is_reduced) {
	if (p_subscript->base == nullptr || p_subscript->index == nullptr) {
		return Variant();
	}

	bool is_base_value_reduced = false;
	Variant base_value = make_expression_reduced_value(p_subscript->base, is_base_value_reduced);
	if (!is_base_value_reduced) {
		return Variant();
	}

	if (p_subscript->is_attribute) {
		bool is_valid = false;
		Variant value = base_value.get_named(p_subscript->attribute->name, is_valid);
		if (is_valid) {
			is_reduced = true;
			return value;
		} else {
			return Variant();
		}
	} else {
		bool is_index_value_reduced = false;
		Variant index_value = make_expression_reduced_value(p_subscript->index, is_index_value_reduced);
		if (!is_index_value_reduced) {
			return Variant();
		}

		bool is_valid = false;
		Variant value = base_value.get(index_value, &is_valid);
		if (is_valid) {
			is_reduced = true;
			return value;
		} else {
			return Variant();
		}
	}
}

Variant FSAnalyzer::make_call_reduced_value(FSParser::CallNode *p_call, bool &is_reduced) {
	if (p_call->get_callee_type() == FSParser::Node::IDENTIFIER) {
		Variant::Type type = Variant::NIL;
		if (p_call->function_name == SNAME("Array")) {
			type = Variant::ARRAY;
		} else if (p_call->function_name == SNAME("Dictionary")) {
			type = Variant::DICTIONARY;
		} else {
			return Variant();
		}

		Vector<Variant> args;
		args.resize(p_call->arguments.size());
		const Variant **argptrs = (const Variant **)alloca(sizeof(const Variant *) * args.size());
		for (int i = 0; i < p_call->arguments.size(); i++) {
			bool is_arg_value_reduced = false;
			Variant arg_value = make_expression_reduced_value(p_call->arguments[i], is_arg_value_reduced);
			if (!is_arg_value_reduced) {
				return Variant();
			}
			args.write[i] = arg_value;
			argptrs[i] = &args[i];
		}

		Variant result;
		Callable::CallError ce;
		Variant::construct(type, result, argptrs, args.size(), ce);
		if (ce.error) {
			push_error(vformat(R"(Failed to construct "%s".)", Variant::get_type_name(type)), p_call);
			return Variant();
		}

		if (type == Variant::ARRAY) {
			Array array = result;
			array.make_read_only();
		} else if (type == Variant::DICTIONARY) {
			Dictionary dictionary = result;
			dictionary.make_read_only();
		}

		is_reduced = true;
		return result;
	}

	return Variant();
}

Array FSAnalyzer::make_array_from_element_datatype(const FSParser::DataType &p_element_datatype, const FSParser::Node *p_source_node) {
	Array array;
	array.set_typed(make_container_type_from_datatype(p_element_datatype, p_source_node));
	return array;
}

ContainerType FSAnalyzer::make_container_type_from_datatype(const FSParser::DataType &p_datatype, const FSParser::Node *p_source_node) {
	ContainerType type;
	type.builtin_type = p_datatype.builtin_type;

	if (p_datatype.builtin_type == Variant::OBJECT) {
		Ref<Script> script_type = p_datatype.script_type;
		if (p_datatype.kind == FSParser::DataType::CLASS && script_type.is_null()) {
			Error err = OK;
			Ref<FoundryScript> scr = get_depended_shallow_script(p_datatype.script_path, err);
			if (err) {
				push_error(vformat(R"(Error while getting cache for script "%s".)", p_datatype.script_path), p_source_node);
				return type;
			}
			script_type.reference_ptr(scr->find_class(p_datatype.class_type->fqcn));
		}
		type.class_name = p_datatype.native_type;
		type.script = script_type;
	}

	for (int i = 0; i < p_datatype.container_element_types.size(); i++) {
		type.element_types.push_back(make_container_type_from_datatype(p_datatype.container_element_types[i], p_source_node));
	}
	for (const FSParser::DataType &argument_type : p_datatype.type_arguments) {
		type.type_arguments.push_back(make_container_type_from_datatype(argument_type, p_source_node));
	}
	return type;
}

Dictionary FSAnalyzer::make_dictionary_from_element_datatype(const FSParser::DataType &p_key_element_datatype, const FSParser::DataType &p_value_element_datatype, const FSParser::Node *p_source_node) {
	Dictionary dictionary;
	dictionary.set_typed(make_container_type_from_datatype(p_key_element_datatype, p_source_node), make_container_type_from_datatype(p_value_element_datatype, p_source_node));
	return dictionary;
}

Variant FSAnalyzer::make_variable_default_value(FSParser::VariableNode *p_variable) {
	Variant result = Variant();

	if (p_variable->initializer) {
		bool is_initializer_value_reduced = false;
		Variant initializer_value = make_expression_reduced_value(p_variable->initializer, is_initializer_value_reduced);
		if (is_initializer_value_reduced) {
			result = initializer_value;
		}
	} else {
		FSParser::DataType datatype = p_variable->get_datatype();
		if (datatype.is_hard_type()) {
			if (datatype.kind == FSParser::DataType::BUILTIN && datatype.builtin_type != Variant::OBJECT) {
				if (datatype.builtin_type == Variant::ARRAY && datatype.has_container_element_type(0)) {
					result = make_array_from_element_datatype(datatype.get_container_element_type(0));
				} else if (datatype.builtin_type == Variant::DICTIONARY && datatype.has_container_element_types()) {
					FSParser::DataType key = datatype.get_container_element_type_or_variant(0);
					FSParser::DataType value = datatype.get_container_element_type_or_variant(1);
					result = make_dictionary_from_element_datatype(key, value);
				} else {
					VariantInternal::initialize(&result, datatype.builtin_type);
				}
			} else if (datatype.kind == FSParser::DataType::ENUM && !datatype.is_tagged_union) {
				// A tagged-union value is a case Array, so no integer stands in as its default; it
				// starts out null like any other type without a constructible zero value.
				result = 0;
			}
		}
	}

	return result;
}

static FSParser::DataType _type_from_container_type(const ContainerType &p_type) {
	FSParser::DataType result;
	result.is_constant = true;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	if (p_type.builtin_type == Variant::NIL) {
		result.kind = FSParser::DataType::VARIANT;
		return result;
	}
	if (p_type.script.is_valid()) {
		result = FSAnalyzer::type_from_metatype(make_script_meta_type(p_type.script));
	} else if (p_type.class_name != StringName()) {
		result = FSAnalyzer::type_from_metatype(make_native_meta_type(p_type.class_name));
	} else {
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = p_type.builtin_type;
	}
	result.is_constant = true;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	for (const ContainerType &element_type : p_type.element_types) {
		result.set_container_element_type(result.get_container_element_type_count(), _type_from_container_type(element_type));
	}
	for (const ContainerType &argument_type : p_type.type_arguments) {
		result.type_arguments.push_back(_type_from_container_type(argument_type));
	}
	return result;
}

FSParser::DataType FSAnalyzer::type_from_variant(const Variant &p_value, const FSParser::Node *p_source) {
	FSParser::DataType result;
	result.is_constant = true;
	result.kind = FSParser::DataType::BUILTIN;
	result.builtin_type = p_value.get_type();
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT; // Constant has explicit type.

	if (p_value.get_type() == Variant::ARRAY) {
		const Array &array = p_value;
		if (array.is_typed()) {
			result.set_container_element_type(0, _type_from_container_type(array.get_element_type()));
		}
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary &dict = p_value;
		if (dict.is_typed_key()) {
			result.set_container_element_type(0, _type_from_container_type(dict.get_key_type()));
		}
		if (dict.is_typed_value()) {
			result.set_container_element_type(1, _type_from_container_type(dict.get_value_type()));
		}
	} else if (p_value.get_type() == Variant::OBJECT) {
		// Object is treated as a native type, not a builtin type.
		result.kind = FSParser::DataType::NATIVE;

		Object *obj = p_value;
		if (!obj) {
			return FSParser::DataType();
		}
		result.native_type = obj->get_class_name();

		Ref<Script> scr = p_value; // Check if value is a script itself.
		if (scr.is_valid()) {
			result.is_meta_type = true;
		} else {
			result.is_meta_type = false;
			scr = obj->get_script();
		}
		if (scr.is_valid()) {
			Ref<FoundryScript> gds = scr;
			if (gds.is_valid()) {
				// This might be an inner class, so we want to get the parser for the root.
				// But still get the inner class from that tree.
				String script_path = gds->get_script_path();
				Ref<FSParserRef> ref;
				Error err = dependency_parser_access.raise_depended_parser_for(script_path, FSParserRef::INHERITANCE_SOLVED, ref);
				if (ref.is_null()) {
					push_error(vformat(R"(Could not find script "%s".)", script_path), p_source);
					FSParser::DataType error_type;
					error_type.kind = FSParser::DataType::VARIANT;
					return error_type;
				}
				FSParser::ClassNode *found = nullptr;
				if (err == OK) {
					found = ref->get_parser()->find_class(gds->fully_qualified_name);
					if (found != nullptr) {
						err = resolve_class_inheritance(found, p_source);
					}
				}
				if (err || found == nullptr) {
					push_error(vformat(R"(Could not resolve script "%s".)", script_path), p_source);
					FSParser::DataType error_type;
					error_type.kind = FSParser::DataType::VARIANT;
					return error_type;
				}

				result.kind = FSParser::DataType::CLASS;
				result.native_type = found->get_datatype().native_type;
				result.class_type = found;
				result.script_path = ref->get_parser()->script_path;
			} else {
				result.kind = FSParser::DataType::SCRIPT;
				result.native_type = scr->get_instance_base_type();
				result.script_path = scr->get_path();
			}
			result.script_type = scr;
		} else {
			result.kind = FSParser::DataType::NATIVE;
			if (result.native_type == FSNativeClass::get_class_static()) {
				result.is_meta_type = true;
			}
		}
	}

	return result;
}

FSParser::DataType FSAnalyzer::type_from_metatype(const FSParser::DataType &p_meta_type) {
	FSParser::DataType result = p_meta_type;
	if (result.is_type_handle_annotation) {
		result.is_pseudo_type = false;
		result.is_constant = false;
		return result;
	}
	result.is_meta_type = false;
	result.is_pseudo_type = false;
	if (p_meta_type.kind == FSParser::DataType::ENUM) {
		// A tagged union's values are read-only `[tag, payload...]` Arrays, so they are deliberately
		// not integers and fail every int-context type check.
		result.builtin_type = p_meta_type.is_tagged_union ? Variant::ARRAY : Variant::INT;
	} else {
		result.is_constant = false;
	}
	return result;
}

FSParser::DataType FSAnalyzer::type_from_property(const PropertyInfo &p_property, bool p_is_arg, bool p_is_readonly) const {
	FSParser::DataType result;
	result.is_read_only = p_is_readonly;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	if (p_property.type == Variant::NIL && (p_is_arg || (p_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT))) {
		// Variant
		result.kind = FSParser::DataType::VARIANT;
		return result;
	}
	result.builtin_type = p_property.type;
	if (p_property.type == Variant::OBJECT) {
		if (p_property.hint == PROPERTY_HINT_COROUTINE_TYPE) {
			// Rebuild Coroutine[T] from the dedicated hint emitted by DataType::to_property_info, so the
			// coroutine identity and phantom result type survive a cross-script PropertyInfo round-trip
			// instead of degrading to a bare FSFunctionState.
			FSParser::DataType coroutine = _decode_coroutine_result_element(p_property.hint_string);
			coroutine.is_read_only = p_is_readonly;
			return coroutine;
		}
		StringName class_name = p_property.class_name;
		if (String(class_name).ends_with("?")) {
			String nullable_class_name = class_name;
			nullable_class_name = nullable_class_name.substr(0, nullable_class_name.length() - 1);
			class_name = nullable_class_name;
			result.is_nullable = true;
		}
		if (ScriptServer::is_global_class(class_name)) {
			result.kind = FSParser::DataType::SCRIPT;
			result.script_path = ScriptServer::get_global_class_path(class_name);
			result.native_type = ScriptServer::get_global_class_native_base(class_name);

			Ref<Script> scr = ResourceLoader::load(ScriptServer::get_global_class_path(class_name));
			if (scr.is_valid()) {
				result.script_type = scr;
			}
		} else {
			result.kind = FSParser::DataType::NATIVE;
			result.native_type = class_name == StringName() ? "Object" : class_name;
		}
	} else {
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = p_property.type;
		if ((p_property.type == Variant::CALLABLE || p_property.type == Variant::SIGNAL) &&
				p_property.hint == PROPERTY_HINT_CALLABLE_TYPE && !p_property.hint_string.is_empty()) {
			// The hint string is only the signature suffix; the leading type name is implied by the
			// property type. An async callable is tagged with an "async" marker on encode (see
			// DataType::to_property_info) because the property type alone cannot express AsyncCallable:
			// "async" alone for a bare AsyncCallable, or "async " followed by the signature suffix.
			String suffix = p_property.hint_string;
			bool is_async = false;
			if (p_property.type == Variant::CALLABLE) {
				if (suffix == "async") {
					is_async = true;
					suffix = "";
				} else if (suffix.begins_with("async ")) {
					is_async = true;
					suffix = suffix.substr(6);
				}
			}
			const String encoded = (p_property.type == Variant::CALLABLE ? String("Callable") : String("Signal")) + suffix;
			const FSParser::DataType decoded = _decode_signature_type(encoded);
			result.signature_is_async = is_async;
			result.has_method_signature = decoded.has_method_signature;
			result.has_explicit_method_signature = decoded.has_explicit_method_signature;
			result.method_parameter_types = decoded.method_parameter_types;
			result.method_return_type = decoded.method_return_type;
			result.method_info = decoded.method_info;
		} else if (p_property.type == Variant::ARRAY && p_property.hint == PROPERTY_HINT_ARRAY_TYPE) {
			// Check element type.
			StringName elem_type_name = p_property.hint_string;
			FSParser::DataType elem_type;
			elem_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;

			if (_container_element_hint_is_coroutine(p_property.hint_string)) {
				// A Coroutine[T] array element is encoded via the signature grammar (see to_property_info),
				// so decode it through the shared coroutine grammar to recover the coroutine identity and
				// result type T instead of resolving "Coroutine[...]" as a bogus class name.
				elem_type = _decode_signature_type(p_property.hint_string);
			} else {
				Variant::Type elem_builtin_type = FSParser::get_builtin_type(elem_type_name);
				if (elem_builtin_type < Variant::VARIANT_MAX) {
					// Builtin type.
					elem_type.kind = FSParser::DataType::BUILTIN;
					elem_type.builtin_type = elem_builtin_type;
				} else if (class_exists(elem_type_name)) {
					elem_type.kind = FSParser::DataType::NATIVE;
					elem_type.builtin_type = Variant::OBJECT;
					elem_type.native_type = elem_type_name;
				} else if (ScriptServer::is_global_class(elem_type_name) && ScriptServer::is_global_class_enum(elem_type_name)) {
					elem_type = make_standalone_global_enum_type(elem_type_name, false);
				} else if (ScriptServer::is_global_class(elem_type_name)) {
					// Just load this as it shouldn't be a FoundryScript.
					Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(elem_type_name));
					elem_type.kind = FSParser::DataType::SCRIPT;
					elem_type.builtin_type = Variant::OBJECT;
					elem_type.native_type = script->get_instance_base_type();
					elem_type.script_type = script;
				} else {
					ERR_FAIL_V_MSG(result, "Could not find element type from property hint of a typed array.");
				}
			}
			elem_type.is_constant = false;
			result.set_container_element_type(0, elem_type);
		} else if (p_property.type == Variant::DICTIONARY && p_property.hint == PROPERTY_HINT_DICTIONARY_TYPE) {
			// Check element type.
			StringName key_elem_type_name = p_property.hint_string.get_slicec(';', 0);
			FSParser::DataType key_elem_type;
			key_elem_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;

			Variant::Type key_elem_builtin_type = FSParser::get_builtin_type(key_elem_type_name);
			if (_container_element_hint_is_coroutine(key_elem_type_name)) {
				// A Coroutine[T] dictionary key is encoded via the signature grammar (see to_property_info),
				// so decode it through the shared coroutine grammar to recover the coroutine identity and
				// result type T instead of resolving it as a bogus class name.
				key_elem_type = _decode_signature_type(key_elem_type_name);
			} else if (key_elem_builtin_type < Variant::VARIANT_MAX) {
				// Builtin type.
				key_elem_type.kind = FSParser::DataType::BUILTIN;
				key_elem_type.builtin_type = key_elem_builtin_type;
			} else if (class_exists(key_elem_type_name)) {
				key_elem_type.kind = FSParser::DataType::NATIVE;
				key_elem_type.builtin_type = Variant::OBJECT;
				key_elem_type.native_type = key_elem_type_name;
			} else if (ScriptServer::is_global_class(key_elem_type_name) && ScriptServer::is_global_class_enum(key_elem_type_name)) {
				key_elem_type = make_standalone_global_enum_type(key_elem_type_name, false);
			} else if (ScriptServer::is_global_class(key_elem_type_name)) {
				// Just load this as it shouldn't be a FoundryScript.
				Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(key_elem_type_name));
				key_elem_type.kind = FSParser::DataType::SCRIPT;
				key_elem_type.builtin_type = Variant::OBJECT;
				key_elem_type.native_type = script->get_instance_base_type();
				key_elem_type.script_type = script;
			} else {
				ERR_FAIL_V_MSG(result, "Could not find element type from property hint of a typed dictionary.");
			}
			key_elem_type.is_constant = false;

			StringName value_elem_type_name = p_property.hint_string.get_slicec(';', 1);
			FSParser::DataType value_elem_type;
			value_elem_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;

			Variant::Type value_elem_builtin_type = FSParser::get_builtin_type(value_elem_type_name);
			if (_container_element_hint_is_coroutine(value_elem_type_name)) {
				// A Coroutine[T] dictionary value is encoded via the signature grammar (see to_property_info),
				// so decode it through the shared coroutine grammar to recover the coroutine identity and
				// result type T instead of resolving it as a bogus class name.
				value_elem_type = _decode_signature_type(value_elem_type_name);
			} else if (value_elem_builtin_type < Variant::VARIANT_MAX) {
				// Builtin type.
				value_elem_type.kind = FSParser::DataType::BUILTIN;
				value_elem_type.builtin_type = value_elem_builtin_type;
			} else if (class_exists(value_elem_type_name)) {
				value_elem_type.kind = FSParser::DataType::NATIVE;
				value_elem_type.builtin_type = Variant::OBJECT;
				value_elem_type.native_type = value_elem_type_name;
			} else if (ScriptServer::is_global_class(value_elem_type_name) && ScriptServer::is_global_class_enum(value_elem_type_name)) {
				value_elem_type = make_standalone_global_enum_type(value_elem_type_name, false);
			} else if (ScriptServer::is_global_class(value_elem_type_name)) {
				// Just load this as it shouldn't be a FoundryScript.
				Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(value_elem_type_name));
				value_elem_type.kind = FSParser::DataType::SCRIPT;
				value_elem_type.builtin_type = Variant::OBJECT;
				value_elem_type.native_type = script->get_instance_base_type();
				value_elem_type.script_type = script;
			} else {
				ERR_FAIL_V_MSG(result, "Could not find element type from property hint of a typed dictionary.");
			}
			value_elem_type.is_constant = false;

			result.set_container_element_type(0, key_elem_type);
			result.set_container_element_type(1, value_elem_type);
		} else if (p_property.type == Variant::INT) {
			// Check if it's enum.
			if ((p_property.usage & PROPERTY_USAGE_CLASS_IS_ENUM) && p_property.class_name != StringName()) {
				if (CoreConstants::is_global_enum(p_property.class_name)) {
					result = make_global_enum_type(p_property.class_name, StringName(), false);
					result.is_constant = false;
				} else if (ScriptServer::is_global_class(p_property.class_name) && ScriptServer::is_global_class_enum(p_property.class_name)) {
					result = make_standalone_global_enum_type(p_property.class_name, false);
					result.is_constant = false;
				} else {
					Vector<String> names = String(p_property.class_name).split(ENUM_SEPARATOR);
					if (names.size() == 2) {
						result = make_enum_type(names[1], names[0], false);
						result.is_constant = false;
					}
				}
			}
			// PROPERTY_USAGE_CLASS_IS_BITFIELD: BitField[T] isn't supported (yet?), use plain int.
		}
	}
	return result;
}

bool FSAnalyzer::get_function_signature(FSParser::Node *p_source, bool p_is_constructor,
		FSParser::DataType p_base_type, const StringName &p_function,
		FSParser::DataType &r_return_type, List<FSParser::DataType> &r_par_types,
		int &r_default_arg_count, BitField<MethodFlags> &r_method_flags,
		StringName *r_native_class, bool *r_is_noreturn,
		FSParser::FunctionNode **r_found_function,
		FSParser::ClassNode **r_found_in_class,
		const FSParser::DataType *p_self_type_override) {
	r_method_flags = METHOD_FLAGS_DEFAULT;
	r_default_arg_count = 0;
	if (r_native_class) {
		*r_native_class = StringName();
	}
	if (r_is_noreturn) {
		*r_is_noreturn = false;
	}
	if (r_found_function) {
		*r_found_function = nullptr;
	}
	if (r_found_in_class) {
		*r_found_in_class = nullptr;
	}
	StringName function_name = p_function;
	FSParser::DataType self_type = p_self_type_override != nullptr ? *p_self_type_override : type_handle_represented_type(p_base_type);

	// A constrained type parameter `[T: Bound]` exposes the methods of its bound, so calls on a
	// `T`-typed value are resolved against `Bound`.
	if (p_base_type.kind == FSParser::DataType::TYPE_PARAMETER && !p_base_type.type_parameter_bound.is_empty()) {
		const bool was_meta_type = p_base_type.is_meta_type;
		p_base_type = p_base_type.type_parameter_bound[0];
		p_base_type.is_meta_type = was_meta_type;
	}

	if (!p_is_constructor && p_base_type.kind == FSParser::DataType::ENUM) {
		FSParser::EnumNode *enum_declaration = resolve_enum_declaration(p_base_type, p_source);
		const int *function_index = enum_declaration != nullptr ? enum_declaration->functions_indices.getptr(function_name) : nullptr;
		if (function_index != nullptr && *function_index >= 0 && *function_index < enum_declaration->functions.size()) {
			FSParser::FunctionNode *found_function = enum_declaration->functions[*function_index];
			const bool dictionary_shadows_instance =
					found_function != nullptr && p_base_type.is_meta_type && !found_function->is_static &&
					p_base_type.builtin_type == Variant::DICTIONARY &&
					Variant::has_builtin_method(Variant::DICTIONARY, function_name);
			if (found_function != nullptr && !dictionary_shadows_instance) {
				if (r_found_function) {
					*r_found_function = found_function;
				}
				if (r_found_in_class) {
					*r_found_in_class = p_base_type.class_type;
				}
				if (r_is_noreturn) {
					*r_is_noreturn = found_function->is_noreturn;
				}
				if (found_function->is_static) {
					r_method_flags.set_flag(METHOD_FLAG_STATIC);
				}
				if (found_function->is_coroutine) {
					r_method_flags.set_flag(METHOD_FLAG_ASYNC);
				}
				if (found_function->is_vararg()) {
					r_method_flags.set_flag(METHOD_FLAG_VARARG);
				}

				const FSParser::DataType enum_value_type = type_handle_represented_type(p_base_type);
				for (FSParser::ParameterNode *parameter : found_function->parameters) {
					r_par_types.push_back(substitute_member_type(
							parameter->get_datatype(), enum_value_type, found_function, &enum_value_type));
					if (parameter->initializer != nullptr) {
						r_default_arg_count++;
					}
				}
				r_return_type = substitute_member_type(
						found_function->get_datatype(), enum_value_type, found_function, &enum_value_type);
				r_return_type.is_meta_type = false;
				if (found_function->is_coroutine) {
					r_return_type = make_coroutine_type(r_return_type);
				}
				return true;
			}
		}
	}

	bool was_enum = false;
	if (p_base_type.kind == FSParser::DataType::ENUM) {
		was_enum = true;
		if (p_base_type.is_meta_type) {
			// Enum type can be treated as a dictionary value.
			p_base_type.kind = FSParser::DataType::BUILTIN;
			p_base_type.is_meta_type = false;
		} else {
			return false;
		}
	}

	if (p_base_type.kind == FSParser::DataType::BUILTIN) {
		const FSParser::CallNode *call = p_source != nullptr && p_source->type == FSParser::Node::CALL ? static_cast<const FSParser::CallNode *>(p_source) : nullptr;
		if (p_base_type.builtin_type == Variant::CALLABLE && p_base_type.is_meta_type && p_function == SNAME("create")) {
			FSParser::DataType callable_type;
			if (call != nullptr && call_site_validation.callable_type_from_constant_method_args(call, 0, 1, callable_type)) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_method_flags.set_flag(METHOD_FLAG_STATIC);
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::NIL, "variant"), true));
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "method"), true));
				r_return_type = callable_type;
				return true;
			}
			if (call != nullptr && call->arguments.size() >= 2) {
				call_site_validation.validate_strict_callable_method_fallback(call, call->arguments[0]->get_datatype(), 1);
			}
		}

		const bool is_callable_call = p_function == SNAME("call");
		const bool is_callable_callv = p_function == SNAME("callv");
		const bool is_callable_call_deferred = p_function == SNAME("call_deferred");
		const bool is_callable_rpc = p_function == SNAME("rpc");
		const bool is_callable_rpc_id = p_function == SNAME("rpc_id");
		if (p_base_type.builtin_type == Variant::CALLABLE && p_base_type.callable_is_over_bound) {
			// An over-bound callable (more arguments bound than its fixed-arity target accepts) can
			// never be invoked successfully, so flag any invocation. bind()/bindv()/unbind() keep
			// producing an over-bound callable so a later invocation is still flagged.
			if (is_callable_call || is_callable_callv || is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id) {
				push_error(R"(Cannot invoke this Callable: it was over-bound (more arguments were bound than its target accepts), so the call can never succeed.)", p_source);
				// Fall through to the generic Callable handling below so the call still resolves to a result type.
			} else if ((p_function == SNAME("bind") || p_function == SNAME("bindv") || p_function == SNAME("unbind")) &&
					Variant::has_builtin_method(Variant::CALLABLE, p_function)) {
				const MethodInfo method_info = Variant::get_builtin_method_info(Variant::CALLABLE, p_function);
				function_signature_from_info(method_info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
				r_return_type = call_site_validation.over_bound_callable_type(p_base_type);
				return true;
			}
		}
		if (p_base_type.builtin_type == Variant::CALLABLE && p_base_type.has_explicit_method_signature) {
			const bool is_callable_vararg = (p_base_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
			auto can_bound_argument_fill_parameter = [&](const FSParser::ExpressionNode *p_argument, const FSParser::DataType &p_parameter_type) -> bool {
				if (p_argument == nullptr) {
					return false;
				}
				if (!p_parameter_type.is_hard_type()) {
					return true;
				}

				FSParser::DataType argument_type = p_argument->get_datatype();
				if (argument_type.is_variant() || !argument_type.is_hard_type()) {
					return false;
				}

				return is_type_compatible(p_parameter_type, argument_type, true);
			};
			auto fixed_vararg_accepts_argument_count = [&](const Vector<const FSParser::ExpressionNode *> &p_bound_arguments, int p_argument_count) -> bool {
				const int fixed_argument_count = p_base_type.method_parameter_types.size();
				const int original_default_arg_count = MIN(p_base_type.method_info.default_arguments.size(), fixed_argument_count);
				const int omitted_argument_count = fixed_argument_count - p_argument_count;
				if (omitted_argument_count <= 0) {
					return true;
				}

				const int bound_filled_count = MIN(omitted_argument_count, p_bound_arguments.size());
				const int default_filled_count = omitted_argument_count - bound_filled_count;
				if (default_filled_count > original_default_arg_count) {
					return false;
				}

				for (int i = 0; i < bound_filled_count; i++) {
					if (!can_bound_argument_fill_parameter(p_bound_arguments[i], p_base_type.method_parameter_types[p_argument_count + i])) {
						return false;
					}
				}

				return true;
			};
			auto fixed_vararg_default_arg_count = [&](const Vector<const FSParser::ExpressionNode *> &p_bound_arguments) -> int {
				const int fixed_argument_count = p_base_type.method_parameter_types.size();

				for (int omitted_argument_count = 1; omitted_argument_count <= fixed_argument_count; omitted_argument_count++) {
					if (!fixed_vararg_accepts_argument_count(p_bound_arguments, fixed_argument_count - omitted_argument_count)) {
						return omitted_argument_count - 1;
					}
				}

				return fixed_argument_count;
			};
			auto preserve_fixed_vararg_callable = [&](const Vector<const FSParser::ExpressionNode *> &p_bound_arguments) -> bool {
				if (!is_callable_vararg) {
					return false;
				}

				const int fixed_argument_count = p_base_type.method_parameter_types.size();
				const int default_arg_count = fixed_vararg_default_arg_count(p_bound_arguments);
				const int continuous_min_argument_count = fixed_argument_count - default_arg_count;
				r_return_type = call_site_validation.transformed_callable_type(p_base_type, p_base_type.method_parameter_types, default_arg_count, true);
				for (int argument_count = 0; argument_count < continuous_min_argument_count; argument_count++) {
					if (fixed_vararg_accepts_argument_count(p_bound_arguments, argument_count)) {
						r_return_type.method_extra_allowed_argument_counts.push_back(argument_count);
					}
				}
				return true;
			};

			if (is_callable_call || is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id) {
				r_default_arg_count = p_base_type.method_info.default_arguments.size();
				r_method_flags = METHOD_FLAGS_DEFAULT;
				if (is_callable_vararg) {
					r_method_flags.set_flag(METHOD_FLAG_VARARG);
				}
				r_return_type = is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id || p_base_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : p_base_type.method_return_type[0];
				if (call != nullptr && is_callable_call && p_base_type.method_return_is_erased_container) {
					static_cast<FSParser::CallNode *>(p_source)->returns_erased_container = true;
				}
				// Synchronously invoking an AsyncCallable yields a coroutine, so the result must be awaited.
				// Deferred/RPC dispatches do not return the callee's value, so they stay non-coroutine.
				if (is_callable_call && p_base_type.signature_is_async) {
					r_return_type = make_coroutine_type(r_return_type);
				}
				if (is_callable_rpc_id) {
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, "peer_id"), true));
				}
				for (const FSParser::DataType &parameter_type : p_base_type.method_parameter_types) {
					r_par_types.push_back(parameter_type);
				}
				return true;
			}

			if (is_callable_callv) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::ARRAY, "arguments"), true));
				r_return_type = p_base_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : p_base_type.method_return_type[0];
				if (call != nullptr && p_base_type.method_return_is_erased_container) {
					static_cast<FSParser::CallNode *>(p_source)->returns_erased_container = true;
				}
				// As with call(), invoking an AsyncCallable through callv() yields a coroutine.
				if (p_base_type.signature_is_async) {
					r_return_type = make_coroutine_type(r_return_type);
				}
				call_site_validation.validate_callable_array_literal_args(p_base_type.method_parameter_types, p_base_type.method_info.default_arguments.size(), is_callable_vararg, call_site_validation.array_literal_argument(call, 0), p_function, p_base_type.method_extra_allowed_argument_counts, p_base_type.method_unbound_argument_count);
				return true;
			}

			if (p_function == SNAME("bind")) {
				r_default_arg_count = 0;
				// Callable.bind() is a variadic builtin: it accepts any number of arguments
				// regardless of the target method's arity. Marking the synthesized signature
				// vararg keeps the leading bound arguments type-checked against the known
				// parameters while permitting extra bound arguments without an arity error.
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_method_flags.set_flag(METHOD_FLAG_VARARG);
				r_return_type = call_site_validation.plain_callable_type();
				if (!is_callable_vararg && call != nullptr) {
					const int bind_argument_count = call->arguments.size();
					const int callable_argument_count = p_base_type.method_parameter_types.size();
					const int checked_bind_argument_count = MIN(bind_argument_count, callable_argument_count);
					const int checked_bind_start = callable_argument_count - checked_bind_argument_count;

					for (int i = 0; i < checked_bind_argument_count; i++) {
						r_par_types.push_back(p_base_type.method_parameter_types[checked_bind_start + i]);
					}

					if (bind_argument_count > callable_argument_count) {
						// Over-binding a fixed-arity callable yields one whose invocation can never
						// succeed; rather than assert a misleading precise signature, fall back to a
						// signatureless callable (keeping the async marker) so downstream call sites
						// are not type-checked against a bogus shape.
						r_return_type = call_site_validation.over_bound_callable_type(p_base_type);
					} else {
						Vector<FSParser::DataType> remaining_parameter_types;
						const int remaining_argument_count = callable_argument_count - bind_argument_count;
						for (int i = 0; i < remaining_argument_count; i++) {
							remaining_parameter_types.push_back(p_base_type.method_parameter_types[i]);
						}

						const int remaining_default_arg_count = MAX(p_base_type.method_info.default_arguments.size() - bind_argument_count, 0);
						r_return_type = call_site_validation.transformed_callable_type(p_base_type, remaining_parameter_types, remaining_default_arg_count, false);
					}
				} else {
					r_method_flags.set_flag(METHOD_FLAG_VARARG);
					if (call != nullptr) {
						Vector<const FSParser::ExpressionNode *> bound_arguments;
						for (FSParser::ExpressionNode *argument : call->arguments) {
							bound_arguments.push_back(argument);
						}
						preserve_fixed_vararg_callable(bound_arguments);
					}
				}
				return true;
			}

			if (p_function == SNAME("bindv")) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::ARRAY, "arguments"), true));
				r_return_type = call_site_validation.plain_callable_type();

				FSParser::ArrayNode *bind_array = call_site_validation.array_literal_argument(call, 0);
				if (!is_callable_vararg && bind_array != nullptr) {
					const int bind_argument_count = bind_array->elements.size();
					const int callable_argument_count = p_base_type.method_parameter_types.size();
					const int checked_bind_argument_count = MIN(bind_argument_count, callable_argument_count);
					const int checked_bind_start = callable_argument_count - checked_bind_argument_count;

					Vector<FSParser::DataType> bound_parameter_types;
					for (int i = 0; i < checked_bind_argument_count; i++) {
						bound_parameter_types.push_back(p_base_type.method_parameter_types[checked_bind_start + i]);
					}
					// Callable.bindv() is variadic over the bound array's contents, so extra
					// elements beyond the target's arity must not raise an arity error; the
					// leading elements are still type-checked against the known parameters.
					call_site_validation.validate_callable_array_literal_args(bound_parameter_types, 0, true, bind_array, p_function);

					if (bind_argument_count > callable_argument_count) {
						// As with bind(), over-binding produces an uninvocable callable; avoid
						// asserting a precise signature for it.
						r_return_type = call_site_validation.over_bound_callable_type(p_base_type);
					} else {
						Vector<FSParser::DataType> remaining_parameter_types;
						const int remaining_argument_count = callable_argument_count - bind_argument_count;
						for (int i = 0; i < remaining_argument_count; i++) {
							remaining_parameter_types.push_back(p_base_type.method_parameter_types[i]);
						}

						const int remaining_default_arg_count = MAX(p_base_type.method_info.default_arguments.size() - bind_argument_count, 0);
						r_return_type = call_site_validation.transformed_callable_type(p_base_type, remaining_parameter_types, remaining_default_arg_count, false);
					}
				} else if (is_callable_vararg && bind_array != nullptr) {
					Vector<const FSParser::ExpressionNode *> bound_arguments;
					for (FSParser::ExpressionNode *argument : bind_array->elements) {
						bound_arguments.push_back(argument);
					}
					preserve_fixed_vararg_callable(bound_arguments);
				}
				return true;
			}

			if (p_function == SNAME("unbind")) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, "argcount"), true));
				r_return_type = call_site_validation.plain_callable_type();

				if (call != nullptr && call->arguments.size() == 1) {
					const FSParser::ExpressionNode *unbind_count_arg = call->arguments[0];
					if (unbind_count_arg->is_constant && unbind_count_arg->reduced_value.get_type() == Variant::INT) {
						const int64_t unbind_argument_count = unbind_count_arg->reduced_value;
						if (unbind_argument_count <= 0) {
							push_error("Amount of \"unbind()\" arguments must be 1 or greater.", unbind_count_arg);
						} else {
							const int unbind_count = int(unbind_argument_count);
							Vector<FSParser::DataType> expanded_parameter_types = p_base_type.method_parameter_types;
							const FSParser::DataType variant_type = type_from_property(PropertyInfo(Variant::NIL, ""), true);
							for (int i = 0; i < unbind_count; i++) {
								expanded_parameter_types.push_back(variant_type);
							}
							r_return_type = call_site_validation.transformed_callable_type(p_base_type, expanded_parameter_types, p_base_type.method_info.default_arguments.size(), is_callable_vararg);
							r_return_type.method_unbound_argument_count = p_base_type.method_unbound_argument_count + unbind_count;
							for (int extra_allowed_argument_count : p_base_type.method_extra_allowed_argument_counts) {
								r_return_type.method_extra_allowed_argument_counts.push_back(extra_allowed_argument_count + unbind_count);
							}
						}
					}
				}
				return true;
			}
		}
		if (p_base_type.builtin_type == Variant::SIGNAL && p_base_type.has_explicit_method_signature && p_function == SNAME("emit")) {
			r_default_arg_count = 0;
			r_method_flags = METHOD_FLAGS_DEFAULT;
			r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
			for (const FSParser::DataType &parameter_type : p_base_type.method_parameter_types) {
				r_par_types.push_back(parameter_type);
			}
			return true;
		}

		if (p_base_type.builtin_type == Variant::ARRAY && p_base_type.has_container_element_type(0)) {
			const bool is_array_single_value_mutation = p_function == SNAME("append") || p_function == SNAME("push_back") ||
					p_function == SNAME("push_front") || p_function == SNAME("fill");
			const bool is_array_indexed_value_mutation = p_function == SNAME("insert") || p_function == SNAME("set");
			const bool is_array_bulk_mutation = p_function == SNAME("assign") || p_function == SNAME("append_array");
			const bool is_array_element_accessor = p_function == SNAME("get") || p_function == SNAME("front") ||
					p_function == SNAME("back") || p_function == SNAME("pick_random") || p_function == SNAME("pop_back") ||
					p_function == SNAME("pop_front") || p_function == SNAME("pop_at");
			const bool is_array_preserved_return = p_function == SNAME("duplicate") || p_function == SNAME("duplicate_deep") || p_function == SNAME("slice");
			if ((is_array_single_value_mutation || is_array_indexed_value_mutation || is_array_bulk_mutation || is_array_element_accessor || is_array_preserved_return) &&
					Variant::has_builtin_method(Variant::ARRAY, p_function)) {
				const MethodInfo method_info = Variant::get_builtin_method_info(Variant::ARRAY, p_function);
				function_signature_from_info(method_info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);

				const FSParser::DataType element_type = p_base_type.get_container_element_type(0);
				FSParser::DataType array_type = type_from_property(PropertyInfo(Variant::ARRAY, "array"), true);
				array_type.set_container_element_type(0, element_type);
				if (is_array_single_value_mutation) {
					r_par_types.clear();
					r_par_types.push_back(element_type);
				} else if (is_array_indexed_value_mutation) {
					r_par_types.clear();
					const StringName index_name = p_function == SNAME("insert") ? SNAME("position") : SNAME("index");
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, index_name), true));
					r_par_types.push_back(element_type);
				} else if (is_array_bulk_mutation) {
					r_par_types.clear();
					r_par_types.push_back(array_type);
				} else if (is_array_element_accessor) {
					r_return_type = element_type;
				} else if (is_array_preserved_return) {
					r_return_type = array_type;
				}
				return true;
			}
		}

		if (p_base_type.builtin_type == Variant::DICTIONARY && p_base_type.has_container_element_types()) {
			const bool is_dictionary_set = p_function == SNAME("set");
			const bool is_dictionary_bulk_mutation = p_function == SNAME("assign") || p_function == SNAME("merge");
			const bool is_dictionary_value_accessor = p_function == SNAME("get") || p_function == SNAME("get_or_add");
			const bool is_dictionary_key_accessor = p_function == SNAME("has") || p_function == SNAME("erase");
			const bool is_dictionary_key_collection_accessor = p_function == SNAME("keys") || p_function == SNAME("has_all");
			const bool is_dictionary_value_collection_accessor = p_function == SNAME("values");
			const bool is_dictionary_value_to_key_accessor = p_function == SNAME("find_key");
			const bool is_dictionary_typed_result = p_function == SNAME("merged") || p_function == SNAME("duplicate") || p_function == SNAME("duplicate_deep");
			if ((is_dictionary_set || is_dictionary_bulk_mutation || is_dictionary_value_accessor || is_dictionary_key_accessor ||
						is_dictionary_key_collection_accessor || is_dictionary_value_collection_accessor || is_dictionary_value_to_key_accessor || is_dictionary_typed_result) &&
					Variant::has_builtin_method(Variant::DICTIONARY, p_function)) {
				const MethodInfo method_info = Variant::get_builtin_method_info(Variant::DICTIONARY, p_function);
				function_signature_from_info(method_info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
				r_par_types.clear();

				const FSParser::DataType key_type = p_base_type.get_container_element_type_or_variant(0);
				const FSParser::DataType value_type = p_base_type.get_container_element_type_or_variant(1);
				FSParser::DataType dictionary_type = type_from_property(PropertyInfo(Variant::DICTIONARY, "dictionary"), true);
				dictionary_type.set_container_element_type(0, key_type);
				dictionary_type.set_container_element_type(1, value_type);
				FSParser::DataType key_array_type = type_from_property(PropertyInfo(Variant::ARRAY, "keys"), true);
				key_array_type.set_container_element_type(0, key_type);

				if (is_dictionary_set) {
					r_par_types.push_back(key_type);
					r_par_types.push_back(value_type);
				} else {
					if (is_dictionary_bulk_mutation || p_function == SNAME("merged")) {
						r_par_types.push_back(dictionary_type);
					}
					if (p_function == SNAME("merge") || p_function == SNAME("merged")) {
						r_par_types.push_back(type_from_property(PropertyInfo(Variant::BOOL, "overwrite"), true));
						if (p_function == SNAME("merged")) {
							r_return_type = dictionary_type;
						}
					} else if (is_dictionary_value_accessor) {
						r_par_types.push_back(key_type);
						r_par_types.push_back(value_type);
						r_return_type = value_type;
					} else if (is_dictionary_key_accessor) {
						r_par_types.push_back(key_type);
					} else if (p_function == SNAME("keys")) {
						r_return_type = key_array_type;
					} else if (p_function == SNAME("has_all")) {
						r_par_types.push_back(key_array_type);
					} else if (p_function == SNAME("values")) {
						FSParser::DataType value_array_type = type_from_property(PropertyInfo(Variant::ARRAY, "values"), true);
						value_array_type.set_container_element_type(0, value_type);
						r_return_type = value_array_type;
					} else if (is_dictionary_value_to_key_accessor) {
						r_par_types.push_back(value_type);
						r_return_type = key_type;
					} else if (is_dictionary_typed_result) {
						for (const PropertyInfo &argument : method_info.arguments) {
							r_par_types.push_back(type_from_property(argument, true));
						}
						r_return_type = dictionary_type;
					}
				}
				return true;
			}
		}

		// Construct a base type to get methods.
		Callable::CallError err;
		Variant dummy;
		Variant::construct(p_base_type.builtin_type, dummy, nullptr, 0, err);
		if (err.error != Callable::CallError::CALL_OK) {
			ERR_FAIL_V_MSG(false, "Could not construct base Variant type.");
		}
		List<MethodInfo> methods;
		dummy.get_method_list(&methods);

		for (const MethodInfo &E : methods) {
			if (E.name == p_function) {
				function_signature_from_info(E, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
				// Invoking a bare AsyncCallable (no explicit signature) still yields a coroutine, so
				// its untyped result must be awaited. The explicit-signature path above handles the
				// AsyncCallable[[...], ...] case; this covers the signatureless `var cb: AsyncCallable`.
				if (p_base_type.builtin_type == Variant::CALLABLE && p_base_type.signature_is_async &&
						(p_function == SNAME("call") || p_function == SNAME("callv"))) {
					r_return_type = make_coroutine_type(r_return_type);
				}
				// Cannot use non-const methods on enums.
				if (!r_method_flags.has_flag(METHOD_FLAG_STATIC) && was_enum && !(E.flags & METHOD_FLAG_CONST)) {
					push_error(vformat(R"*(Cannot call non-const Dictionary function "%s()" on enum "%s".)*", p_function, p_base_type.enum_type), p_source);
				}
				return true;
			}
		}

		return false;
	}

	if (p_base_type.is_coroutine) {
		// Coroutine[T] is opaque: its only source operation is await, so it exposes no callable
		// members. Report it as "not found" via the caller's generic diagnostic rather than falling
		// through to native resolution, which would leak the FSFunctionState skin.
		return false;
	}

	StringName base_native = p_base_type.native_type;
	if (base_native != StringName()) {
		// Empty native class might happen in some Script implementations.
		// Just ignore it.
		if (!class_exists(base_native)) {
			push_error(vformat("Native class %s used in script doesn't exist or isn't exposed.", base_native), p_source);
			return false;
		} else if (p_is_constructor && ClassDB::is_abstract(base_native)) {
			if (p_base_type.kind == FSParser::DataType::CLASS) {
				push_error(vformat(R"(Class "%s" cannot be constructed as it is based on abstract native class "%s".)", p_base_type.class_type->fqcn.get_file(), base_native), p_source);
			} else if (p_base_type.kind == FSParser::DataType::SCRIPT) {
				push_error(vformat(R"(Script "%s" cannot be constructed as it is based on abstract native class "%s".)", p_base_type.script_path.get_file(), base_native), p_source);
			} else {
				push_error(vformat(R"(Native class "%s" cannot be constructed as it is abstract.)", base_native), p_source);
			}
			return false;
		}
	}

	if (p_is_constructor) {
		function_name = FSLanguage::get_singleton()->strings._init;
		r_method_flags.set_flag(METHOD_FLAG_STATIC);
	}

	FSParser::ClassNode *base_class = p_base_type.class_type;
	FSParser::ClassNode *original_base_class = base_class;
	FSParser::FunctionNode *found_function = nullptr;
	// The class that declares `found_function`, used to specialize a generic signature against the
	// (possibly more-derived) receiver type so inherited `T`-typed parameters/returns become concrete.
	FSParser::ClassNode *found_in_class = nullptr;

	while (found_function == nullptr && base_class != nullptr) {
		if (base_class->has_member(function_name)) {
			const FSParser::ClassNode::Member &member = base_class->get_member(function_name);
			if (member.type != FSParser::ClassNode::Member::FUNCTION) {
				const FSParser::DataType member_type = member.get_datatype();
				if (member_type.is_set() && member_type.kind == FSParser::DataType::BUILTIN && member_type.builtin_type == Variant::CALLABLE) {
					return false;
				}
				push_error(vformat(R"(Member "%s" is not a function.)", function_name), p_source);
				return false;
			}

			resolve_class_member(base_class, function_name, p_source);
			found_function = member.function;
			found_in_class = base_class;
		}

		resolve_class_inheritance(base_class, p_source);
		base_class = base_class->base_type.class_type;
	}

	// Resolve calls to methods flattened in from applied traits, both when the base is a
	// trait (trait-requires-trait) and when it is a class that applies traits directly.
	if (found_function == nullptr && original_base_class != nullptr && (original_base_class->is_trait || !original_base_class->used_traits.is_empty())) {
		resolve_trait_uses(original_base_class, p_source);
		for (FSParser::ClassNode *trait : original_base_class->resolved_traits) {
			if (trait == nullptr || !trait->has_member(function_name)) {
				continue;
			}

			const FSParser::ClassNode::Member &member = trait->get_member(function_name);
			if (member.type != FSParser::ClassNode::Member::FUNCTION) {
				const FSParser::DataType member_type = member.get_datatype();
				if (member_type.is_set() && member_type.kind == FSParser::DataType::BUILTIN && member_type.builtin_type == Variant::CALLABLE) {
					return false;
				}
				push_error(vformat(R"(Member "%s" is not a function.)", function_name), p_source);
				return false;
			}

			resolve_class_member(trait, function_name, p_source);
			found_function = member.function;
			found_in_class = trait;
			break;
		}
	}

	if (found_function != nullptr) {
		if (r_found_function) {
			*r_found_function = found_function;
		}
		if (r_found_in_class) {
			*r_found_in_class = found_in_class;
		}
		if (found_function->is_abstract) {
			r_method_flags.set_flag(METHOD_FLAG_VIRTUAL_REQUIRED);
		}
		if (r_is_noreturn) {
			*r_is_noreturn = found_function->is_noreturn;
		}
		if (p_is_constructor || found_function->is_static) {
			r_method_flags.set_flag(METHOD_FLAG_STATIC);
		}
		if (found_function->is_coroutine) {
			r_method_flags.set_flag(METHOD_FLAG_ASYNC);
		}
		// Specialize the signature against the receiver so inherited or directly-applied type
		// arguments substitute `T`-typed parameters and return into concrete types.
		const FSParser::DataType specialized_base = specialize_ancestor_type(p_base_type, found_in_class);
		// Parameter-position `Self` is an exact receiver contract for ordinary calls, where dynamic
		// overrides may narrow it. Static calls and signature validation use concrete substitution.
		const bool parameter_self_is_receiver_contract =
				p_self_type_override == nullptr && !p_is_constructor && !found_function->is_static;
		const FSParser::DataType parameter_self_type =
				parameter_self_is_receiver_contract ? _self_type_parameter_from_bound(self_type) : self_type;
		for (int i = 0; i < found_function->parameters.size(); i++) {
			r_par_types.push_back(substitute_member_type(
					found_function->parameters[i]->get_datatype(), specialized_base, found_function, &parameter_self_type));
			if (found_function->parameters[i]->initializer != nullptr) {
				r_default_arg_count++;
			}
		}
		if (found_function->is_vararg()) {
			r_method_flags.set_flag(METHOD_FLAG_VARARG);
		}
		if (p_source != nullptr && p_source->type == FSParser::Node::CALL &&
				original_base_class != nullptr && found_in_class != nullptr && found_in_class != original_base_class &&
				!found_in_class->is_trait) {
			if (_datatype_container_element_contains_self_type_parameter(found_function->get_datatype())) {
				static_cast<FSParser::CallNode *>(p_source)->returns_erased_container = true;
			}
		}
		r_return_type = p_is_constructor ? p_base_type : substitute_member_type(found_function->get_datatype(), specialized_base, found_function, &self_type);
		r_return_type.is_meta_type = false;
		if (found_function->is_coroutine) {
			r_return_type = make_coroutine_type(r_return_type);
		}

		return true;
	}

	Ref<Script> base_script = p_base_type.script_type;

	while (base_script.is_valid() && base_script->has_method(function_name)) {
		MethodInfo info = base_script->get_method_info(function_name);

		if (!(info == MethodInfo())) {
			return function_signature_from_info(info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
		}
		base_script = base_script->get_base_script();
	}

	// If the base is a script, it might be trying to access members of the Script class itself.
	if (p_base_type.is_meta_type && !p_is_constructor && (p_base_type.kind == FSParser::DataType::SCRIPT || p_base_type.kind == FSParser::DataType::CLASS)) {
		MethodInfo info;
		StringName script_class = p_base_type.kind == FSParser::DataType::SCRIPT ? p_base_type.script_type->get_class_name() : StringName(FoundryScript::get_class_static());

		if (ClassDB::get_method_info(script_class, function_name, &info)) {
			return function_signature_from_info(info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
		}
	}

	auto argument_can_be_string_name = [&](const FSParser::CallNode *p_call, int p_argument_index) -> bool {
		if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return false;
		}

		const FSParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
		if (argument == nullptr) {
			return false;
		}

		if (argument->is_constant) {
			const Variant::Type value_type = argument->reduced_value.get_type();
			return value_type == Variant::STRING || value_type == Variant::STRING_NAME;
		}

		const FSParser::DataType argument_type = argument->get_datatype();
		if (argument_type.is_variant() || !argument_type.is_hard_type()) {
			return true;
		}

		const FSParser::DataType string_name_type = type_from_property(PropertyInfo(Variant::STRING_NAME, ""), true);
		const FSParser::DataType string_type = type_from_property(PropertyInfo(Variant::STRING, ""), true);
		return is_type_compatible(string_name_type, argument_type, true) || is_type_compatible(string_type, argument_type, true);
	};
	auto argument_can_be_node_path = [&](const FSParser::CallNode *p_call, int p_argument_index) -> bool {
		if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return false;
		}

		const FSParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
		if (argument == nullptr) {
			return false;
		}

		if (argument->is_constant) {
			return argument->reduced_value.get_type() == Variant::NODE_PATH;
		}

		const FSParser::DataType argument_type = argument->get_datatype();
		if (argument_type.is_variant() || !argument_type.is_hard_type()) {
			return true;
		}

		const FSParser::DataType node_path_type = type_from_property(PropertyInfo(Variant::NODE_PATH, ""), true);
		return is_type_compatible(node_path_type, argument_type, true);
	};
	auto push_strict_dynamic_reflection_error = [&](const FSParser::CallNode *p_call, int p_argument_index, const char *p_kind) {
		if (!strict_dynamic_checks || p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return;
		}
		push_error(vformat(R"*(Cannot use dynamic %s for "%s()" on type "%s" in strict dynamic mode.)*",
						   p_kind,
						   p_call->function_name,
						   p_base_type.to_string()),
				p_call->arguments[p_argument_index]);
	};
	auto push_strict_unresolved_reflection_error = [&](const FSParser::CallNode *p_call, int p_argument_index, const char *p_kind, const String &p_name) {
		if (!strict_dynamic_checks || p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return;
		}
		String kind = p_kind;
		if (kind.ends_with(" name")) {
			kind = kind.substr(0, kind.length() - 5);
		}
		push_error(vformat(R"*(Cannot resolve %s "%s" on type "%s" for "%s()" in strict dynamic mode.)*",
						   kind,
						   p_name,
						   p_base_type.to_string(),
						   p_call->function_name),
				p_call->arguments[p_argument_index]);
	};

	if (p_function == SNAME("call") || p_function == SNAME("call_deferred") || p_function == SNAME("callv")) {
		const FSParser::CallNode *call = p_source != nullptr && p_source->type == FSParser::Node::CALL ? static_cast<const FSParser::CallNode *>(p_source) : nullptr;
		StringName method_name;
		const bool has_constant_method_name = call != nullptr && string_name_from_constant_arg(call, 0, method_name);
		if (has_constant_method_name && method_name != SNAME("call") && method_name != SNAME("call_deferred") && method_name != SNAME("callv")) {
			FSParser::DataType callable_type;
			if (call_site_validation.callable_type_from_method(p_base_type, method_name, p_source, callable_type)) {
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "method"), true));
				if (p_function == SNAME("callv")) {
					r_default_arg_count = 0;
					r_return_type = callable_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : callable_type.method_return_type[0];
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::ARRAY, "arg_array"), true));
					call_site_validation.validate_callable_array_literal_args(callable_type.method_parameter_types, callable_type.method_info.default_arguments.size(), callable_type.method_info.flags & METHOD_FLAG_VARARG, call_site_validation.array_literal_argument(call, 1), p_function);
				} else {
					r_default_arg_count = callable_type.method_info.default_arguments.size();
					if (callable_type.method_info.flags & METHOD_FLAG_VARARG) {
						r_method_flags.set_flag(METHOD_FLAG_VARARG);
					}
					r_return_type = p_function == SNAME("call_deferred") || callable_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : callable_type.method_return_type[0];
					for (const FSParser::DataType &parameter_type : callable_type.method_parameter_types) {
						r_par_types.push_back(parameter_type);
					}
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, 0, "method", method_name);
		} else if (!has_constant_method_name && argument_can_be_string_name(call, 0)) {
			push_strict_dynamic_reflection_error(call, 0, "method name");
		}
	}

	if ((p_function == SNAME("rpc") || p_function == SNAME("rpc_id")) && is_node_compatible_type(p_base_type)) {
		const FSParser::CallNode *call = p_source != nullptr && p_source->type == FSParser::Node::CALL ? static_cast<const FSParser::CallNode *>(p_source) : nullptr;
		const int method_arg_index = p_function == SNAME("rpc_id") ? 1 : 0;
		StringName method_name;
		const bool has_constant_method_name = call != nullptr && string_name_from_constant_arg(call, method_arg_index, method_name);
		if (has_constant_method_name && method_name != SNAME("rpc") && method_name != SNAME("rpc_id")) {
			FSParser::DataType callable_type;
			if (call_site_validation.callable_type_from_method(p_base_type, method_name, p_source, callable_type)) {
				r_default_arg_count = callable_type.method_info.default_arguments.size();
				r_method_flags = METHOD_FLAGS_DEFAULT;
				if (callable_type.method_info.flags & METHOD_FLAG_VARARG) {
					r_method_flags.set_flag(METHOD_FLAG_VARARG);
				}
				r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
				if (p_function == SNAME("rpc_id")) {
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, "peer_id"), true));
				}
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "method"), true));
				for (const FSParser::DataType &parameter_type : callable_type.method_parameter_types) {
					r_par_types.push_back(parameter_type);
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, method_arg_index, "method", method_name);
		} else if (!has_constant_method_name && argument_can_be_string_name(call, method_arg_index)) {
			push_strict_dynamic_reflection_error(call, method_arg_index, "method name");
		}
	}

	if (p_function == SNAME("get") || p_function == SNAME("set") || p_function == SNAME("set_deferred")) {
		const FSParser::CallNode *call = p_source != nullptr && p_source->type == FSParser::Node::CALL ? static_cast<const FSParser::CallNode *>(p_source) : nullptr;
		StringName property_name;
		const bool has_constant_property_name = call != nullptr && string_name_from_constant_arg(call, 0, property_name);
		if (has_constant_property_name) {
			FSParser::DataType property_type;
			if (property_type_from_receiver(p_base_type, property_name, p_source, property_type)) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "property"), true));
				if (p_function == SNAME("get")) {
					r_return_type = property_type;
				} else {
					r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
					r_par_types.push_back(property_type);
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, 0, "property", property_name);
		} else if (!has_constant_property_name && argument_can_be_string_name(call, 0)) {
			push_strict_dynamic_reflection_error(call, 0, "property name");
		}
	}

	if (p_function == SNAME("get_indexed") || p_function == SNAME("set_indexed")) {
		const FSParser::CallNode *call = p_source != nullptr && p_source->type == FSParser::Node::CALL ? static_cast<const FSParser::CallNode *>(p_source) : nullptr;
		Vector<StringName> property_path;
		const bool has_constant_property_path = call != nullptr && property_path_from_constant_arg(call, 0, property_path);
		if (has_constant_property_path) {
			FSParser::DataType property_type;
			if (property_type_from_indexed_receiver(p_base_type, property_path, p_source, property_type)) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::NODE_PATH, "property_path"), true));
				if (p_function == SNAME("get_indexed")) {
					r_return_type = property_type;
				} else {
					r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
					r_par_types.push_back(property_type);
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, 0, "property path", String(NodePath(call->arguments[0]->reduced_value)));
		} else if (!has_constant_property_path && argument_can_be_node_path(call, 0)) {
			push_strict_dynamic_reflection_error(call, 0, "property path");
		}
	}

	if (p_is_constructor) {
		// Native types always have a default constructor.
		r_return_type = p_base_type;
		r_return_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		r_return_type.is_meta_type = false;
		return true;
	}

	MethodInfo info;
	if (ClassDB::get_method_info(base_native, function_name, &info)) {
		bool valid = function_signature_from_info(info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
		if (valid && Engine::get_singleton()->has_singleton(base_native)) {
			r_method_flags.set_flag(METHOD_FLAG_STATIC);
		}
		if (valid && r_is_noreturn && base_native == SNAME("OS") && function_name == SNAME("crash")) {
			*r_is_noreturn = true;
		}
#ifdef DEBUG_ENABLED
		MethodBind *native_method = ClassDB::get_method(base_native, function_name);
		if (native_method && r_native_class) {
			*r_native_class = native_method->get_instance_class();
		}
#endif // DEBUG_ENABLED
		return valid;
	}

	return false;
}

bool FSAnalyzer::function_signature_from_info(const MethodInfo &p_info, FSParser::DataType &r_return_type, List<FSParser::DataType> &r_par_types, int &r_default_arg_count, BitField<MethodFlags> &r_method_flags) {
	r_return_type = type_from_property(p_info.return_val);
	// METHOD_FLAG_ASYNC wraps the declared return type into Coroutine[T]. MethodInfo stores the declared
	// return type in return_val, so this wraps unconditionally to mirror the in-memory async call-site
	// path: an async method declared `-> Coroutine[T]` yields Coroutine[Coroutine[T]], same as locally.
	if ((p_info.flags & METHOD_FLAG_ASYNC) != 0) {
		r_return_type = make_coroutine_type(r_return_type);
	}
	r_default_arg_count = p_info.default_arguments.size();
	r_method_flags = p_info.flags;

	for (const PropertyInfo &E : p_info.arguments) {
		r_par_types.push_back(type_from_property(E, true));
	}
	return true;
}

bool FSAnalyzer::string_name_from_constant_arg(const FSParser::CallNode *p_call, int p_argument_index, StringName &r_name) const {
	if (p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return false;
	}

	const FSParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr || !argument->is_constant) {
		return false;
	}

	const Variant value = argument->reduced_value;
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		return false;
	}

	r_name = value;
	return true;
}

bool FSAnalyzer::resolve_explicit_type_argument(FSParser::ExpressionNode *p_expression, FSParser::DataType &r_type_argument) {
	if (p_expression == nullptr) {
		return false;
	}

	if (p_expression->type == FSParser::Node::IDENTIFIER) {
		FSParser::IdentifierNode *identifier = static_cast<FSParser::IdentifierNode *>(p_expression);
		const Variant::Type builtin_type = FSParser::get_builtin_type(identifier->name);
		if (builtin_type < Variant::VARIANT_MAX) {
			r_type_argument = type_from_metatype(make_builtin_meta_type(builtin_type));
			return true;
		}

		FSParser::DataType type_parameter;
		if (resolve_type_parameter(identifier->name, type_parameter)) {
			r_type_argument = type_parameter;
			return true;
		}

		if (identifier->name == SNAME("Self") && parser->current_class != nullptr) {
			FSParser::DataType enum_type = enum_self_type(parser->current_function);
			if (enum_type.is_set()) {
				r_type_argument = enum_type;
				return true;
			}

			const bool receiver_relative_self = resolving_function_signature_type || parser->current_function != nullptr || parser->current_class->is_trait;
			if (receiver_relative_self) {
				if (parser->current_function != nullptr) {
					parser->current_function->uses_receiver_relative_self = true;
				}
				r_type_argument = _self_type_parameter_for_class(parser->current_class);
			} else {
				FSParser::DataType self_type = _self_type_for_class(parser->current_class);
				self_type.is_meta_type = true;
				r_type_argument = type_from_metatype(self_type);
			}
			return true;
		}

		reduce_identifier(identifier, true);
		FSParser::DataType identifier_type = identifier->get_datatype();
		if (identifier_type.is_set() && identifier_type.is_meta_type) {
			r_type_argument = type_from_metatype(identifier_type);
			return true;
		}
		return false;
	}

	if (p_expression->type == FSParser::Node::SUBSCRIPT) {
		// A nested type argument parsed as a subscript, such as `Array[Element]` (single element) or
		// `Dictionary[Key, Value]` (a two-argument container). A generic class handle (`Box[int]`)
		// as a nested argument is not representable here yet, so reject it rather than silently
		// building a wrong type.
		FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute || subscript->base == nullptr || subscript->index == nullptr) {
			return false;
		}
		FSParser::DataType base_argument;
		if (!resolve_explicit_type_argument(subscript->base, base_argument)) {
			return false;
		}
		if (base_argument.kind != FSParser::DataType::BUILTIN) {
			return false;
		}

		Vector<FSParser::ExpressionNode *> element_expressions;
		if (subscript->type_arguments.is_empty()) {
			element_expressions.push_back(subscript->index);
		} else {
			element_expressions = subscript->type_arguments;
		}

		if (base_argument.builtin_type == Variant::ARRAY) {
			if (element_expressions.size() != 1) {
				return false;
			}
		} else if (base_argument.builtin_type == Variant::DICTIONARY) {
			if (element_expressions.size() != 2) {
				return false;
			}
		} else {
			return false;
		}

		for (int i = 0; i < element_expressions.size(); i++) {
			FSParser::DataType element_argument;
			if (!resolve_explicit_type_argument(element_expressions[i], element_argument)) {
				return false;
			}
			base_argument.set_container_element_type(i, element_argument);
		}
		r_type_argument = base_argument;
		return true;
	}

	return false;
}

void FSAnalyzer::reduce_call_create_proxy(FSParser::CallNode *p_call, FSParser::SubscriptNode *p_callee) {
	// The built-in `create_proxy[T](handler) -> T`: a typed layer over the
	// `create_proxy_dynamic` runtime. The analyzer types the result as T and flags
	// the node; the compiler lowers it to `create_proxy_dynamic(T, handler)` by
	// materializing T's script from the `[T]` type argument.
	FSParser::DataType error_type;
	error_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	error_type.kind = FSParser::DataType::VARIANT;

	// Read the full use-site type-argument list positionally. A single argument is
	// captured in `index`; a multi-argument list (`create_proxy[A, B]`) populates
	// `type_arguments`, with `index` aliasing the first element. `create_proxy[T]`
	// takes exactly one type parameter, so anything other than a single argument is an
	// arity error rather than silently consuming only the first entry.
	Vector<FSParser::ExpressionNode *> argument_expressions;
	if (p_callee->type_arguments.is_empty()) {
		if (p_callee->index != nullptr) {
			argument_expressions.push_back(p_callee->index);
		}
	} else {
		argument_expressions = p_callee->type_arguments;
	}

	if (argument_expressions.size() != 1) {
		push_error(vformat(R"*(create_proxy[T]() expects a single type argument, but %d %s given.)*", argument_expressions.size(), argument_expressions.size() == 1 ? "was" : "were"), p_callee->index != nullptr ? static_cast<FSParser::Node *>(p_callee->index) : static_cast<FSParser::Node *>(p_call));
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// Resolve the `[T]` type argument. This also reduces a class-name index as a value,
	// which the compiler later compiles into T's script reference.
	FSParser::DataType type_argument;
	if (!resolve_explicit_type_argument(argument_expressions[0], type_argument)) {
		push_error(R"*(Could not resolve the type argument for "create_proxy[T]()".)*", argument_expressions[0]);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// A forwarded class type parameter is reified onto the instance at construction
	// (e.g. `Mock[Greeter].new()` binds T = Greeter), so the compiler can materialize its
	// bound script at runtime and the runtime guard validates the actual binding. A method
	// type parameter is not reified onto the instance, so it still has no concrete script
	// to recover; reject only that case, pointing at the dynamic fallback.
	if (type_argument.kind == FSParser::DataType::TYPE_PARAMETER &&
			type_argument.type_parameter_scope != FSParser::DataType::TYPE_PARAMETER_CLASS) {
		push_error(vformat(R"*(create_proxy[T]() cannot forward the method type parameter "%s" because method type arguments are not reified at runtime. Use create_proxy_dynamic() with an explicit type instead.)*", type_argument.to_string()), p_call);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// A forwarded class type parameter resolves to its bound script only at runtime, so the
	// remaining static checks (trait/abstract and RefCounted-base guards) cannot run here;
	// they are enforced by the runtime guard in `FSProxy::create_proxy` against the
	// actual binding. Type the result as T and let the compiler emit the reified lookup.
	if (type_argument.kind == FSParser::DataType::TYPE_PARAMETER) {
		// Class type parameters are reified per instance, so they are only recoverable from a
		// non-static function. A static function has no instance to read the binding from.
		if (static_context) {
			push_error(vformat(R"*(create_proxy[T]() cannot forward the class type parameter "%s" from a static function because it is reified per instance. Use create_proxy_dynamic() with an explicit type instead.)*", type_argument.to_string()), p_call);
			p_call->set_datatype(error_type);
			mark_node_unsafe(p_call);
			return;
		}

		// The compiler materializes T from the instance's reified bindings, so this call
		// needs `self`. Inside a lambda that does not otherwise touch `self`, mark it as
		// using self so it is invoked with the instance rather than a null one.
		mark_lambda_use_self();

		type_argument.is_meta_type = false;
		type_argument.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		p_call->is_proxy_construct = true;
		p_call->set_datatype(type_argument);

		if (p_call->arguments.size() != 1) {
			push_error(vformat(R"*(create_proxy[T]() expects a single handler argument, but %d %s given.)*", p_call->arguments.size(), p_call->arguments.size() == 1 ? "was" : "were"), p_call);
		} else {
			const FSParser::DataType handler_type = p_call->arguments[0]->get_datatype();
			if (handler_type.is_hard_type() && !handler_type.is_variant() && !(handler_type.kind == FSParser::DataType::BUILTIN && handler_type.builtin_type == Variant::CALLABLE)) {
				push_error(vformat(R"*(create_proxy[T]() expects a Callable handler, but the argument is of type "%s".)*", handler_type.to_string()), p_call->arguments[0]);
			}
		}
		return;
	}

	// The target must be a trait or abstract type (mirrors the runtime guard in
	// FSProxy::create_proxy): only those define a contract to intercept. When the
	// type cannot be classified statically (e.g. an external script not yet compiled), the
	// runtime guard still applies, so this only hard-rejects the cases known to be invalid.
	bool target_invalid = false;
	switch (type_argument.kind) {
		case FSParser::DataType::CLASS:
			if (type_argument.class_type != nullptr) {
				target_invalid = !(type_argument.class_type->is_trait || type_argument.class_type->is_abstract);
			}
			break;
		case FSParser::DataType::SCRIPT: {
			Ref<FoundryScript> foundry_script = type_argument.script_type;
			if (foundry_script.is_valid() && foundry_script->is_valid()) {
				target_invalid = !(foundry_script->is_trait_type() || foundry_script->is_abstract());
			}
		} break;
		case FSParser::DataType::BUILTIN:
		case FSParser::DataType::NATIVE:
		case FSParser::DataType::ENUM:
			target_invalid = true;
			break;
		default:
			break;
	}
	if (target_invalid) {
		push_error(vformat(R"*(create_proxy[T]() requires a trait or abstract type as its type argument, but "%s" is neither.)*", type_argument.to_string()), p_call);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// Mirror the runtime guard in FSProxy::_validate_proxy_target: the proxy host is a
	// RefCounted, so a contract rooted on any other native base (Node, Resource, ...) cannot be
	// represented soundly. Resolve the native base the same way is_node_compatible_type does; if
	// it cannot be determined statically, defer to the runtime guard rather than false-reject.
	StringName native_base = type_argument.native_type;
	if (native_base == StringName() && type_argument.kind == FSParser::DataType::CLASS) {
		const FSParser::ClassNode *class_node = type_argument.class_type;
		while (class_node != nullptr) {
			if (class_node->base_type.kind == FSParser::DataType::CLASS) {
				class_node = class_node->base_type.class_type;
				continue;
			}
			if (class_node->base_type.native_type != StringName()) {
				native_base = class_node->base_type.native_type;
			} else if (class_node->base_type.script_type.is_valid()) {
				native_base = class_node->base_type.script_type->get_instance_base_type();
			}
			break;
		}
	}
	// The runtime guard requires the base to be exactly RefCounted (not merely a descendant such
	// as Resource), since proxying other native bases is not supported yet. Match that exactly.
	if (native_base != StringName() && native_base != SNAME("RefCounted") && class_exists(native_base)) {
		push_error(vformat(R"*(create_proxy[T]() requires a trait or abstract type rooted on RefCounted, but "%s" has native base "%s".)*", type_argument.to_string(), native_base), p_call);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// Exactly one handler argument, which must be callable.
	if (p_call->arguments.size() != 1) {
		push_error(vformat(R"*(create_proxy[T]() expects a single handler argument, but %d %s given.)*", p_call->arguments.size(), p_call->arguments.size() == 1 ? "was" : "were"), p_call);
	} else {
		const FSParser::DataType handler_type = p_call->arguments[0]->get_datatype();
		if (handler_type.is_hard_type() && !handler_type.is_variant() && !(handler_type.kind == FSParser::DataType::BUILTIN && handler_type.builtin_type == Variant::CALLABLE)) {
			push_error(vformat(R"*(create_proxy[T]() expects a Callable handler, but the argument is of type "%s".)*", handler_type.to_string()), p_call->arguments[0]);
		}
	}

	// The result is an instance of T.
	type_argument.is_meta_type = false;
	type_argument.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	p_call->is_proxy_construct = true;
	p_call->set_datatype(type_argument);
}

bool FSAnalyzer::is_node_compatible_type(const FSParser::DataType &p_type) const {
	StringName native_type = p_type.native_type;

	if (native_type == StringName() && p_type.kind == FSParser::DataType::CLASS) {
		const FSParser::ClassNode *class_node = p_type.class_type;
		while (class_node != nullptr) {
			if (class_node->base_type.kind == FSParser::DataType::CLASS) {
				class_node = class_node->base_type.class_type;
				continue;
			}
			if (class_node->base_type.native_type != StringName()) {
				native_type = class_node->base_type.native_type;
			} else if (class_node->base_type.script_type.is_valid()) {
				native_type = class_node->base_type.script_type->get_instance_base_type();
			}
			break;
		}
	}

	return native_type != StringName() && class_exists(native_type) && ClassDB::is_parent_class(native_type, SNAME("Node"));
}

bool FSAnalyzer::property_type_from_class(FSParser::ClassNode *p_class, const StringName &p_property_name, FSParser::Node *p_source, FSParser::DataType &r_property_type) {
	for (FSParser::ClassNode *class_node = p_class; class_node != nullptr;) {
		if (class_node->has_member(p_property_name)) {
			if (class_node->get_member(p_property_name).type != FSParser::ClassNode::Member::VARIABLE) {
				return false;
			}

			resolve_class_member(class_node, p_property_name, p_source);
			r_property_type = class_node->get_member(p_property_name).get_datatype();
			return true;
		}

		resolve_class_inheritance(class_node, p_source);
		if (class_node->base_type.kind == FSParser::DataType::CLASS) {
			class_node = class_node->base_type.class_type;
		} else if (class_node->base_type.kind == FSParser::DataType::SCRIPT) {
			return property_type_from_script(class_node->base_type.script_type, p_property_name, r_property_type);
		} else if (class_node->base_type.kind == FSParser::DataType::NATIVE) {
			return property_type_from_native(class_node->base_type.native_type, p_property_name, r_property_type);
		} else {
			class_node = nullptr;
		}
	}

	return false;
}

bool FSAnalyzer::property_type_from_script(const Ref<Script> &p_script, const StringName &p_property_name, FSParser::DataType &r_property_type) const {
	Ref<Script> script = p_script;
	while (script.is_valid()) {
		List<PropertyInfo> property_list;
		script->get_script_property_list(&property_list);

		for (const PropertyInfo &property_info : property_list) {
			if (property_info.name == p_property_name) {
				r_property_type = type_from_property(property_info);
				return true;
			}
		}

		script = script->get_base_script();
	}

	if (p_script.is_valid()) {
		return property_type_from_native(p_script->get_instance_base_type(), p_property_name, r_property_type);
	}

	return false;
}

bool FSAnalyzer::property_type_from_native(const StringName &p_native_type, const StringName &p_property_name, FSParser::DataType &r_property_type) const {
	if (p_native_type == StringName() || !class_exists(p_native_type) || !ClassDB::has_property(p_native_type, p_property_name)) {
		return false;
	}

	const StringName getter_name = ClassDB::get_property_getter(p_native_type, p_property_name);
	MethodBind *getter = getter_name == StringName() ? nullptr : ClassDB::get_method(p_native_type, getter_name);
	if (getter != nullptr) {
		const bool is_read_only = ClassDB::get_property_setter(p_native_type, p_property_name) == StringName();
		r_property_type = type_from_property(getter->get_return_info(), false, is_read_only);
		return true;
	}

	bool is_valid = false;
	const Variant::Type property_type = ClassDB::get_property_type(p_native_type, p_property_name, &is_valid);
	if (!is_valid) {
		return false;
	}

	r_property_type = type_from_property(PropertyInfo(property_type, String(p_property_name)));
	return true;
}

bool FSAnalyzer::property_type_from_receiver(const FSParser::DataType &p_receiver_type, const StringName &p_property_name, FSParser::Node *p_source, FSParser::DataType &r_property_type) {
	switch (p_receiver_type.kind) {
		case FSParser::DataType::CLASS:
			return property_type_from_class(p_receiver_type.class_type, p_property_name, p_source, r_property_type);
		case FSParser::DataType::SCRIPT:
			return property_type_from_script(p_receiver_type.script_type, p_property_name, r_property_type);
		case FSParser::DataType::NATIVE:
			return property_type_from_native(p_receiver_type.native_type, p_property_name, r_property_type);
		default:
			return false;
	}
}

bool FSAnalyzer::property_path_from_constant_arg(const FSParser::CallNode *p_call, int p_argument_index, Vector<StringName> &r_property_path) const {
	if (p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return false;
	}

	const FSParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr || !argument->is_constant || argument->reduced_value.get_type() != Variant::NODE_PATH) {
		return false;
	}

	const NodePath property_path = NodePath(argument->reduced_value).get_as_property_path();
	r_property_path = property_path.get_subnames();
	return !r_property_path.is_empty();
}

bool FSAnalyzer::property_type_from_builtin_member(const FSParser::DataType &p_base_type, const StringName &p_member_name, FSParser::DataType &r_member_type) const {
	if (p_base_type.kind != FSParser::DataType::BUILTIN) {
		return false;
	}

	const Variant::Type builtin_type = p_base_type.builtin_type;
	if (builtin_type == Variant::OBJECT || builtin_type == Variant::ARRAY || builtin_type == Variant::DICTIONARY || builtin_type == Variant::NIL) {
		return false;
	}
	if (Variant::get_member_validated_getter(builtin_type, p_member_name) == nullptr) {
		return false;
	}

	const Variant::Type member_type = Variant::get_member_type(builtin_type, p_member_name);
	if (member_type == Variant::VARIANT_MAX) {
		return false;
	}

	r_member_type = type_from_property(PropertyInfo(member_type, String(p_member_name)));
	return true;
}

bool FSAnalyzer::property_type_from_indexed_receiver(const FSParser::DataType &p_receiver_type, const Vector<StringName> &p_property_path, FSParser::Node *p_source, FSParser::DataType &r_property_type) {
	if (p_property_path.is_empty()) {
		return false;
	}

	if (!property_type_from_receiver(p_receiver_type, p_property_path[0], p_source, r_property_type)) {
		return false;
	}

	for (int i = 1; i < p_property_path.size(); i++) {
		if (!property_type_from_builtin_member(r_property_type, p_property_path[i], r_property_type)) {
			return false;
		}
	}

	return true;
}

#ifdef DEBUG_ENABLED
void FSAnalyzer::is_shadowing(FSParser::IdentifierNode *p_identifier, const String &p_context, const bool p_in_local_scope) {
	const StringName &name = p_identifier->name;

	{
		List<MethodInfo> fs_funcs;
		FSLanguage::get_singleton()->get_public_functions(&fs_funcs);

		for (MethodInfo &info : fs_funcs) {
			if (info.name == name) {
				parser->push_warning(p_identifier, FSWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "built-in function");
				return;
			}
		}
		if (Variant::has_utility_function(name)) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "built-in function");
			return;
		} else if (class_exists(name)) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "native class");
			return;
		} else if (ScriptServer::is_global_class(name)) {
			String class_path = ScriptServer::get_global_class_path(name).get_file();
			parser->push_warning(p_identifier, FSWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, vformat(R"(global class defined in "%s")", class_path));
			return;
		} else if (FSParser::get_builtin_type(name) < Variant::VARIANT_MAX) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "built-in type");
			return;
		}
	}

	const FSParser::DataType current_class_type = parser->current_class->get_datatype();
	if (p_in_local_scope) {
		FSParser::ClassNode *base_class = current_class_type.class_type;

		if (base_class != nullptr) {
			if (base_class->has_member(name)) {
				parser->push_warning(p_identifier, FSWarning::SHADOWED_VARIABLE, p_context, p_identifier->name, base_class->get_member(name).get_type_name(), itos(base_class->get_member(name).get_line()));
				return;
			}
			base_class = base_class->base_type.class_type;
		}

		while (base_class != nullptr) {
			if (base_class->has_member(name)) {
				String base_class_name = base_class->get_global_name();
				if (base_class_name.is_empty()) {
					base_class_name = base_class->fqcn;
				}

				parser->push_warning(p_identifier, FSWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, base_class->get_member(name).get_type_name(), itos(base_class->get_member(name).get_line()), base_class_name);
				return;
			}
			base_class = base_class->base_type.class_type;
		}
	}

	StringName native_base_class = current_class_type.native_type;
	while (native_base_class != StringName()) {
		ERR_FAIL_COND_MSG(!class_exists(native_base_class), "Non-existent native base class.");

		if (ClassDB::has_method(native_base_class, name, true)) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "method", native_base_class);
			return;
		} else if (ClassDB::has_signal(native_base_class, name, true)) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "signal", native_base_class);
			return;
		} else if (ClassDB::has_property(native_base_class, name, true)) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "property", native_base_class);
			return;
		} else if (ClassDB::has_integer_constant(native_base_class, name, true)) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "constant", native_base_class);
			return;
		} else if (ClassDB::has_enum(native_base_class, name, true)) {
			parser->push_warning(p_identifier, FSWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "enum", native_base_class);
			return;
		}
		native_base_class = ClassDB::get_parent_class(native_base_class);
	}
}
#endif // DEBUG_ENABLED

FSParser::DataType FSAnalyzer::get_operation_type(Variant::Operator p_operation, const FSParser::DataType &p_a, bool &r_valid, const FSParser::Node *p_source) {
	// Unary version.
	FSParser::DataType nil_type;
	nil_type.builtin_type = Variant::NIL;
	nil_type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
	return get_operation_type(p_operation, p_a, nil_type, r_valid, p_source);
}

FSParser::DataType FSAnalyzer::get_operation_type(Variant::Operator p_operation, const FSParser::DataType &p_a, const FSParser::DataType &p_b, bool &r_valid, const FSParser::Node *p_source) {
	if (p_operation == Variant::OP_AND || p_operation == Variant::OP_OR) {
		// Those work for any type of argument and always return a boolean.
		// They don't use the Variant operator since they have short-circuit semantics.
		r_valid = true;
		FSParser::DataType result;
		result.type_source = FSParser::DataType::ANNOTATED_INFERRED;
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::BOOL;
		return result;
	}

	Variant::Type a_type = p_a.builtin_type;
	Variant::Type b_type = p_b.builtin_type;

	// A tagged-union value is a read-only `[tag, payload...]` Array, so it is never an integer.
	if (p_a.kind == FSParser::DataType::ENUM) {
		if (p_a.is_meta_type) {
			a_type = Variant::DICTIONARY;
		} else {
			a_type = p_a.is_tagged_union ? Variant::ARRAY : Variant::INT;
		}
	}
	if (p_b.kind == FSParser::DataType::ENUM) {
		if (p_b.is_meta_type) {
			b_type = Variant::DICTIONARY;
		} else {
			b_type = p_b.is_tagged_union ? Variant::ARRAY : Variant::INT;
		}
	}

	// The Array erasure is a representation detail, not part of the union's surface: only identity
	// comparison is meaningful on a case value. Concatenation, containment, and the other Array
	// operators would otherwise leak through and silently produce a plain Array.
	if ((p_a.is_tagged_union_type() && !p_a.is_meta_type) || (p_b.is_tagged_union_type() && !p_b.is_meta_type)) {
		if (p_operation != Variant::OP_EQUAL && p_operation != Variant::OP_NOT_EQUAL) {
			r_valid = !(p_a.is_hard_type() && p_b.is_hard_type());
			FSParser::DataType invalid;
			invalid.kind = FSParser::DataType::VARIANT;
			return invalid;
		}
	}

	FSParser::DataType result;
	bool hard_operation = p_a.is_hard_type() && p_b.is_hard_type();

	if (p_operation == Variant::OP_ADD && a_type == Variant::ARRAY && b_type == Variant::ARRAY) {
		if (p_a.has_container_element_type(0) && p_b.has_container_element_type(0)) {
			if (p_a.get_container_element_type(0) == p_b.get_container_element_type(0)) {
				r_valid = true;
				result = p_a;
				result.type_source = hard_operation ? FSParser::DataType::ANNOTATED_INFERRED : FSParser::DataType::INFERRED;
				return result;
			}

			r_valid = false;
			result.kind = FSParser::DataType::BUILTIN;
			result.builtin_type = Variant::ARRAY;
			result.type_source = hard_operation ? FSParser::DataType::ANNOTATED_INFERRED : FSParser::DataType::INFERRED;
			return result;
		}
	}

	Variant::ValidatedOperatorEvaluator op_eval = Variant::get_validated_operator_evaluator(p_operation, a_type, b_type);
	bool validated = op_eval != nullptr;

	if (validated) {
		r_valid = true;
		result.type_source = hard_operation ? FSParser::DataType::ANNOTATED_INFERRED : FSParser::DataType::INFERRED;
		result.kind = FSParser::DataType::BUILTIN;
		result.builtin_type = Variant::get_operator_return_type(p_operation, a_type, b_type);
	} else {
		r_valid = !hard_operation;
		result.kind = FSParser::DataType::VARIANT;
	}

	return result;
}

bool FSAnalyzer::is_type_compatible(const FSParser::DataType &p_target, const FSParser::DataType &p_source, bool p_allow_implicit_conversion, const FSParser::Node *p_source_node) {
#ifdef DEBUG_ENABLED
	if (p_source_node) {
		// A tagged union rejects ints outright, so the "cast it" advice would be wrong there.
		if (p_target.kind == FSParser::DataType::ENUM && !p_target.is_tagged_union) {
			if (p_source.kind == FSParser::DataType::BUILTIN && p_source.builtin_type == Variant::INT) {
				parser->push_warning(p_source_node, FSWarning::INT_AS_ENUM_WITHOUT_CAST);
			}
		}
	}
#endif // DEBUG_ENABLED
	// Global class references are often resolved only to `INHERITANCE_SOLVED` (`make_global_class_meta_type`),
	// so `resolved_traits` may still be empty when a trait-typed assignment, argument, or cast is checked.
	// Eagerly resolve trait uses on the argument class chain before consulting `_class_has_trait`.
	if (p_target.kind == FSParser::DataType::CLASS && p_target.class_type != nullptr &&
			p_target.class_type->is_trait && !p_target.is_meta_type && !p_source.is_meta_type &&
			p_source.kind == FSParser::DataType::CLASS && p_source.class_type != nullptr) {
		return type_satisfies_trait(p_source, p_target);
	}
	FSTypeCompatibility::Options options;
	options.allow_implicit_conversion = p_allow_implicit_conversion;
	options.strict_dynamic = strict_dynamic_checks;
	options.strict_null = strict_null_checks;
	return FSTypeCompatibility::check(p_target, p_source, options).compatible;
}

bool FSAnalyzer::check_type_compatibility(const FSParser::DataType &p_target, const FSParser::DataType &p_source, bool p_allow_implicit_conversion, const FSParser::Node *p_source_node) {
	(void)p_source_node;
	FSTypeCompatibility::Options options;
	options.allow_implicit_conversion = p_allow_implicit_conversion;
	return FSTypeCompatibility::check(p_target, p_source, options).compatible;
}

void FSAnalyzer::push_error(const String &p_message, const FSParser::Node *p_origin) {
	mark_node_unsafe(p_origin);
	parser->push_error(p_message, p_origin);
}

void FSAnalyzer::mark_node_unsafe(const FSParser::Node *p_node) {
#ifdef DEBUG_ENABLED
	if (p_node == nullptr) {
		return;
	}

	for (int i = p_node->start_line; i <= p_node->end_line; i++) {
		parser->unsafe_lines.insert(i);
	}
#endif // DEBUG_ENABLED
}

void FSAnalyzer::downgrade_node_type_source(FSParser::Node *p_node) {
	FSParser::IdentifierNode *identifier = nullptr;
	if (p_node->type == FSParser::Node::IDENTIFIER) {
		identifier = static_cast<FSParser::IdentifierNode *>(p_node);
	} else if (p_node->type == FSParser::Node::SUBSCRIPT) {
		FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(p_node);
		if (subscript->is_attribute) {
			identifier = subscript->attribute;
		}
	}
	if (identifier == nullptr) {
		return;
	}

	FSParser::Node *source = nullptr;
	switch (identifier->source) {
		case FSParser::IdentifierNode::MEMBER_VARIABLE: {
			source = identifier->variable_source;
		} break;
		case FSParser::IdentifierNode::FUNCTION_PARAMETER: {
			source = identifier->parameter_source;
		} break;
		case FSParser::IdentifierNode::LOCAL_VARIABLE: {
			source = identifier->variable_source;
		} break;
		case FSParser::IdentifierNode::LOCAL_ITERATOR: {
			source = identifier->bind_source;
		} break;
		default:
			break;
	}
	if (source == nullptr) {
		return;
	}

	FSParser::DataType datatype;
	datatype.kind = FSParser::DataType::VARIANT;
	source->set_datatype(datatype);
}

void FSAnalyzer::mark_lambda_use_self() {
	FSParser::LambdaNode *lambda = current_lambda;
	while (lambda != nullptr) {
		lambda->use_self = true;
		lambda = lambda->parent_lambda;
	}
}

void FSAnalyzer::resolve_pending_lambda_bodies() {
	if (pending_body_resolution_lambdas.is_empty()) {
		return;
	}

	FSParser::LambdaNode *previous_lambda = current_lambda;
	bool previous_static_context = static_context;

	List<FSParser::LambdaNode *> lambdas = pending_body_resolution_lambdas;
	pending_body_resolution_lambdas.clear();

	for (FSParser::LambdaNode *lambda : lambdas) {
		current_lambda = lambda;
		static_context = lambda->function->is_static;

		resolve_function_body(lambda->function, true);

		int captures_amount = lambda->captures.size();
		if (captures_amount > 0) {
			// Create space for lambda parameters.
			// At the beginning to not mess with optional parameters.
			int param_count = lambda->function->parameters.size();
			lambda->function->parameters.resize(param_count + captures_amount);
			for (int i = param_count - 1; i >= 0; i--) {
				lambda->function->parameters.write[i + captures_amount] = lambda->function->parameters[i];
				lambda->function->parameters_indices[lambda->function->parameters[i]->identifier->name] = i + captures_amount;
			}

			// Add captures as extra parameters at the beginning.
			for (int i = 0; i < lambda->captures.size(); i++) {
				FSParser::IdentifierNode *capture = lambda->captures[i];
				FSParser::ParameterNode *capture_param = parser->alloc_node<FSParser::ParameterNode>();
				capture_param->identifier = capture;
				capture_param->usages = capture->usages;
				capture_param->set_datatype(capture->get_datatype());

				lambda->function->parameters.write[i] = capture_param;
				lambda->function->parameters_indices[capture->name] = i;
			}
		}
	}

	current_lambda = previous_lambda;
	static_context = previous_static_context;
}

bool FSAnalyzer::class_exists(const StringName &p_class) {
	return ClassDB::class_exists(p_class) && ClassDB::is_class_exposed(p_class);
}

uint32_t FSAnalyzer::get_autoload_settings_hash() const {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings == nullptr) {
		return 0;
	}

	uint32_t hash = hash_murmur3_one_32(project_settings->get_autoload_list().size());
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &kv : project_settings->get_autoload_list()) {
		const ProjectSettings::AutoloadInfo &autoload = kv.value;
		hash = hash_murmur3_one_32(autoload.name.hash(), hash);
		hash = hash_murmur3_one_32(autoload.path.hash(), hash);
		hash = hash_murmur3_one_32(autoload.is_singleton ? 1 : 0, hash);
	}
	return hash_fmix32(hash);
}

void FSAnalyzer::ensure_autoload_index_current() {
	const uint32_t settings_hash = get_autoload_settings_hash();
	if (autoload_index.get_version() > 0 && autoload_index_settings_hash == settings_hash) {
		return;
	}

	autoload_index.rebuild_from_project_settings();
	autoload_index_settings_hash = settings_hash;
}

Error FSAnalyzer::resolve_inheritance() {
	return run_phase_inheritance_resolution();
}

Error FSAnalyzer::resolve_interface() {
	Error err = run_phase_interface_and_member_surface();
	if (err) {
		return err;
	}
	return run_phase_trait_conformance_registration();
}

Error FSAnalyzer::resolve_body() {
	Error err = run_phase_body_expression_callable_signal();
	if (err) {
		return err;
	}

	err = run_phase_conformance_witness_body();
	if (err) {
		return err;
	}

	return run_phase_finalize_analyzer_warnings();
}

Error FSAnalyzer::resolve_dependencies() {
	return run_phase_final_diagnostics_and_dependencies();
}

// Canonical analyzer phase order:
// 0 Preflight -> 1 Dependency/parse availability (on-demand) -> 2 Inheritance resolution ->
// 3 Interface and member surface -> 4 Trait conformance registration ->
// 5 Body/expression/callable/signal analysis -> 6 Flow/finality invariants ->
// 7 Conformance witness bodies -> 8 Final diagnostics and dependency finalization.
Error FSAnalyzer::analyze() {
	Error err = run_phase_preflight();
	if (err) {
		return err;
	}

	err = run_phase_inheritance_resolution();
	if (err) {
		return err;
	}

	// Interface and conformance diagnostics are collected even when earlier phases reported errors.
	run_phase_interface_and_member_surface();
	run_phase_trait_conformance_registration();

	err = run_phase_body_expression_callable_signal();
	if (err) {
		return err;
	}

	err = run_phase_conformance_witness_body();
	if (err) {
		return err;
	}

	run_phase_finalize_analyzer_warnings();
	return run_phase_final_diagnostics_and_dependencies();
}

bool FSAnalyzer::call_argument_is_same_receiver(const FSParser::CallNode *p_call, const FSParser::ExpressionNode *p_argument) const {
	return ::call_argument_is_same_receiver(p_call, p_argument);
}

bool FSAnalyzer::datatype_contains_self_type_parameter(const FSParser::DataType &p_type) const {
	return _datatype_contains_self_type_parameter(p_type);
}

bool FSAnalyzer::is_bare_self_value_parameter(const FSParser::DataType &p_type) const {
	return _is_bare_self_value_parameter(p_type);
}

bool FSAnalyzer::datatype_matches_self_parameter_contract(const FSParser::DataType &p_expected_type, const FSParser::DataType &p_argument_type) const {
	return _datatype_matches_self_parameter_contract(p_expected_type, p_argument_type);
}

String FSAnalyzer::make_type_handle_argument_error(
		const StringName &p_function,
		int p_argument_number,
		const FSParser::DataType &p_expected_type,
		const FSParser::DataType &p_actual_type,
		const FSParser::Node *p_actual_node) const {
	return _make_type_handle_argument_error(p_function, p_argument_number, p_expected_type, p_actual_type, p_actual_node);
}

void FSAnalyzer::mark_coroutine_handle_capture(FSParser::ExpressionNode *p_expression, const FSParser::DataType &p_target_type) {
	::mark_coroutine_handle_capture(p_expression, p_target_type);
}

bool FSAnalyzer::signature_type_involves_type_parameter(const FSParser::DataType &p_type) const {
	return _signature_type_involves_type_parameter(p_type);
}

FSAnalyzer::FSAnalyzer(FSParser *p_parser) :
		parser(p_parser),
		dependency_parser_access(this),
		flow_finality(this),
		call_site_validation(this) {
	strict_null_checks = GLOBAL_GET_CACHED(bool, "debug/foundry_script/analysis/strict_null_checks");
	strict_dynamic_checks = GLOBAL_GET_CACHED(bool, "debug/foundry_script/analysis/strict_dynamic_checks");
}
