/**************************************************************************/
/*  fs_analyzer.h                                                         */
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

#pragma once

#include "fs_autoload_index.h"
#include "fs_cache.h"
#include "fs_conformance_registry.h"
#include "fs_parser.h"

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/variant/container_type_validate.h"

#ifdef TESTS_ENABLED
namespace FSTests {
class TestFSAnalyzerAccessor;
class TestFSAnalyzerDependencyAccessAccessor;
} //namespace FSTests
#endif // TESTS_ENABLED

class FSAnalyzer {
public:
	// Canonical analyzer phase order (see `analyze()` for orchestration):
	// 0 Preflight
	// 1 Dependency/parse availability (on-demand during later phases)
	// 2 Inheritance resolution
	// 3 Interface and member surface resolution
	// 4 Trait conformance registration
	// 5 Body, expression, callable, and signal analysis
	// 6 Flow/finality and trait body invariants
	// 7 Conformance witness body analysis
	// 8 Final diagnostics and dependency finalization
	enum class AnalyzerPhase : int8_t {
		NONE = -1,
		PREFLIGHT = 0,
		DEPENDENCY_PARSE_AVAILABILITY = 1,
		INHERITANCE_RESOLUTION = 2,
		INTERFACE_AND_MEMBER_SURFACE = 3,
		TRAIT_CONFORMANCE_REGISTRATION = 4,
		BODY_EXPRESSION_CALLABLE_SIGNAL = 5,
		FLOW_FINALITY_INVARIANTS = 6,
		CONFORMANCE_WITNESS_BODY = 7,
		FINAL_DIAGNOSTICS_AND_DEPENDENCIES = 8,
	};

private:
	FSParser *parser = nullptr;
	FSAutoloadIndex autoload_index;
	uint32_t autoload_index_settings_hash = 0;
	AnalyzerPhase highest_completed_phase = AnalyzerPhase::NONE;
	bool suppress_internal_phase_order_checks = false;

	struct TraitMethodImplementation {
		FSParser::FunctionNode *function = nullptr;
		FSParser::ClassNode *owner_class = nullptr;
		MethodInfo method_info;
		String method_info_source;
		bool has_method_info = false;
	};

	template <typename Fn>
	class Finally {
		Fn fn;

	public:
		Finally(Fn p_fn) :
				fn(p_fn) {}
		~Finally() {
			fn();
		}
	};

	// Restores `parser->current_class`, `parser->current_function`, `current_enum`,
	// `current_lambda`, and `static_context` at scope exit.
	class AnalysisScopeGuard {
		FSAnalyzer *analyzer = nullptr;
		FSParser::ClassNode *previous_class = nullptr;
		FSParser::FunctionNode *previous_function = nullptr;
		const FSParser::EnumNode *previous_enum = nullptr;
		FSParser::LambdaNode *previous_lambda = nullptr;
		bool previous_static_context = false;

	public:
		AnalysisScopeGuard(FSAnalyzer *p_analyzer, FSParser::ClassNode *p_class, FSParser::FunctionNode *p_function = nullptr);
		~AnalysisScopeGuard();
	};

	// Phase 1 helper: dependency/parser-cache access for external scripts and classes.
	// Requested status guide:
	// - PARSED: cache-walk lookup and external class delegation before local phase work.
	// - INHERITANCE_SOLVED: extends chains, global class/enum metadata, trait identity, autoload typing.
	// - INTERFACE_SOLVED: annotation libraries, trait surfaces, conformance targets, preload conformances.
	// - FULLY_SOLVED: directly applied external trait bodies before flattening (body phase exception).
	class DependencyParserAccess {
		FSAnalyzer *analyzer = nullptr;
		HashMap<const FSParser::ClassNode *, Ref<FSParserRef>> external_class_parser_cache;

		void mark_dependency_phase_completed();

	public:
		explicit DependencyParserAccess(FSAnalyzer *p_analyzer);

		Ref<FSParserRef> ensure_cached_external_parser_for_class(const FSParser::ClassNode *p_class, const FSParser::ClassNode *p_from_class, const char *p_context, const FSParser::Node *p_source);
		Ref<FSParserRef> find_cached_external_parser_for_class(const FSParser::ClassNode *p_class, const Ref<FSParserRef> &p_dependant_parser);
		Ref<FSParserRef> find_cached_external_parser_for_class(const FSParser::ClassNode *p_class, FSParser *p_dependant_parser);

		Ref<FSParserRef> depended_parser_for(const String &p_path, FSParserRef::Status p_required_status);
		Error raise_depended_parser_for(const String &p_path, FSParserRef::Status p_required_status, Ref<FSParserRef> &r_parser_ref);
		Error raise_parser_to_status(const Ref<FSParserRef> &p_parser_ref, FSParserRef::Status p_required_status);

#ifdef TESTS_ENABLED
		int test_external_class_parser_cache_size() const {
			return external_class_parser_cache.size();
		}
#endif // TESTS_ENABLED
	};

	// Marks dependency/parse availability for one analyzer lookup sequence.
	class DependencyParserAccessScope {
		FSAnalyzer *analyzer = nullptr;

	public:
		explicit DependencyParserAccessScope(FSAnalyzer *p_analyzer);
		DependencyParserAccess &access();
		~DependencyParserAccessScope();
	};

	DependencyParserAccess dependency_parser_access;

	// Decides which files' retroactive conformances this analysis is allowed to honor: its own, and
	// whatever it transitively depends on. A conformance takes effect for code that loads its declaring
	// file, so honoring one from an unrelated file would type-check code that fails at run time, and
	// would make results depend on which files the process happened to analyze first.
	class ConformanceVisibility : public FSConformanceRegistry::Visibility {
		FSAnalyzer *analyzer = nullptr;
		// Memoized per analysis. The dependency graph only grows during one analysis, so a `true` stays
		// true; a `false` is not cached, since a file may become a dependency later on.
		mutable HashSet<String> visible_files;

	public:
		explicit ConformanceVisibility(FSAnalyzer *p_analyzer);
		bool can_see(const String &p_source_file) const override;
	};

	ConformanceVisibility conformance_visibility;

	// Owns flow-sensitive narrowing state and definite-assignment analysis for `final` variables.
	// Lifetime: `flow_narrowed_types` and `flow_narrowing_captured_sources` are active during body
	// resolution (one function body at a time via `FlowNarrowingScope`); `flattened_trait_final_nodes`
	// is populated only for the duration of a final-member/static pass.
	class FlowFinalityContext {
		FSAnalyzer *analyzer = nullptr;

		HashMap<const FSParser::Node *, FSParser::DataType> flow_narrowed_types;
		HashMap<const FSParser::Node *, bool> flow_narrowing_captured_sources;
		HashSet<const FSParser::VariableNode *> flattened_trait_final_nodes;

	public:
		class FlowNarrowingScope {
			FlowFinalityContext *context = nullptr;
			HashMap<const FSParser::Node *, FSParser::DataType> previous_flow_narrowed_types;
			HashMap<const FSParser::Node *, bool> previous_flow_narrowing_captured_sources;
			bool restore_captured_sources = false;

		public:
			FlowNarrowingScope(FlowFinalityContext &p_context, bool p_track_captured_sources);
			~FlowNarrowingScope();
		};

		class FlattenedTraitFinalNodesScope {
			FlowFinalityContext *context = nullptr;

		public:
			explicit FlattenedTraitFinalNodesScope(FlowFinalityContext &p_context);
			void insert(const FSParser::VariableNode *p_variable);
			~FlattenedTraitFinalNodesScope();
		};

		struct FinalAssignmentState {
			HashSet<const FSParser::VariableNode *> assigned;
			HashSet<const FSParser::VariableNode *> maybe_assigned;
			bool reachable = true;
		};
		enum class FinalAssignmentScope {
			INSTANCE_MEMBER,
			STATIC_MEMBER,
			LOCAL,
		};

		explicit FlowFinalityContext(FSAnalyzer *p_analyzer);

		const FSParser::Node *flow_narrowing_key_from_identifier(const FSParser::IdentifierNode *p_identifier) const;
		void apply_flow_narrowing(const FSParser::IdentifierNode *p_identifier);
		void apply_flow_narrowing(const FSParser::IdentifierNode *p_identifier, const FSParser::DataType &p_type);
		void apply_match_branch_flow_narrowing(FSParser::ExpressionNode *p_match_test, FSParser::MatchBranchNode *p_match_branch);
		void clear_flow_narrowing(const FSParser::ExpressionNode *p_expression);
		void mark_flow_narrowing_capture(const FSParser::IdentifierNode *p_identifier);
		void clear_captured_flow_narrowing();
		bool null_check_narrowing_identifier(FSParser::ExpressionNode *p_condition, bool p_condition_value, FSParser::IdentifierNode *&r_identifier) const;
		bool type_test_narrowing_identifier(FSParser::ExpressionNode *p_condition, bool p_condition_value, FSParser::IdentifierNode *&r_identifier, FSParser::DataType &r_type) const;
		void reduce_condition_expression(FSParser::ExpressionNode *p_condition);
		void apply_flow_narrowing_from_condition(FSParser::ExpressionNode *p_condition, bool p_condition_value);
		const FSParser::DataType *lookup_flow_narrowed_type(const FSParser::Node *p_key) const;
		HashMap<const FSParser::Node *, FSParser::DataType> &get_flow_narrowed_types() { return flow_narrowed_types; }
		const HashMap<const FSParser::Node *, FSParser::DataType> &get_flow_narrowed_types() const { return flow_narrowed_types; }

		void check_final_member_assignments(FSParser::ClassNode *p_class);
		void check_final_static_assignments(FSParser::ClassNode *p_class);
		void check_final_local_assignments(FSParser::ClassNode *p_class);
		void analyze_function_local_finals(const FSParser::FunctionNode *p_function);
		void collect_local_finals(const FSParser::Node *p_node,
				HashSet<const FSParser::VariableNode *> &r_finals,
				HashMap<StringName, const FSParser::VariableNode *> &r_finals_by_name);
		static void merge_final_assignment_branches(const FinalAssignmentState &p_first, const FinalAssignmentState &p_second, FinalAssignmentState &r_out);
		const FSParser::VariableNode *final_member_assignment_target(const FSParser::ExpressionNode *p_expression,
				const HashSet<const FSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool *r_is_self_receiver = nullptr, bool p_flattened_trait_body = false) const;
		void scan_illegal_final_writes(const FSParser::Node *p_node,
				const HashSet<const FSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool p_in_init, bool p_flattened_trait_body = false);
		void analyze_final_definite_assignment_suite(const FSParser::SuiteNode *p_suite,
				const HashSet<const FSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
				HashSet<const FSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body = false);
		void analyze_final_definite_assignment_statement(const FSParser::Node *p_statement,
				const HashSet<const FSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
				HashSet<const FSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body = false);
		void check_final_reads_in_expression(const FSParser::ExpressionNode *p_expression,
				const HashSet<const FSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, const FinalAssignmentState &p_state, bool p_flattened_trait_body = false);
		void check_final_reads_in_pattern(const FSParser::PatternNode *p_pattern,
				const HashSet<const FSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, const FinalAssignmentState &p_state, bool p_flattened_trait_body = false);
	};

	FlowFinalityContext flow_finality;

	// Owns callable argument validation, named-argument canonicalization, generic method call
	// inference at call sites, and signal connect/emit API validation during body analysis.
	class CallSiteValidationContext {
		FSAnalyzer *analyzer = nullptr;

	public:
		explicit CallSiteValidationContext(FSAnalyzer *p_analyzer);

		bool merge_inferred_type_argument(const FSParser::DataType &p_existing, const FSParser::DataType &p_candidate, FSParser::DataType &r_merged);
		void collect_type_parameter_bindings(const FSParser::DataType &p_parameter_type, const FSParser::DataType &p_argument_type,
				HashMap<StringName, FSParser::DataType> &r_bindings, HashSet<StringName> &r_conflicts);
		void apply_generic_method_call(FSParser::CallNode *p_call, FSParser::FunctionNode *p_function,
				List<FSParser::DataType> &r_par_types, FSParser::DataType &r_return_type);
		bool callable_signature_from_type(const FSParser::DataType &p_callable_type, Vector<FSParser::DataType> &r_par_types, int &r_default_arg_count, bool &r_is_vararg) const;
		FSParser::DataType plain_callable_type() const;
		FSParser::DataType over_bound_callable_type(const FSParser::DataType &p_source_callable_type) const;
		FSParser::DataType explicit_callable_type_from_signature(const FSParser::DataType &p_return_type, const Vector<FSParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg, bool p_is_async = false) const;
		FSParser::DataType transformed_callable_type(const FSParser::DataType &p_source_callable_type, const Vector<FSParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const;
		FSParser::DataType explicit_callable_type_from_info(const MethodInfo &p_info) const;
		FSParser::DataType explicit_signal_type_from_info(const MethodInfo &p_info) const;
		FSParser::DataType explicit_signal_type_from_node(const FSParser::SignalNode *p_signal) const;
		FSParser::ArrayNode *array_literal_argument(const FSParser::CallNode *p_call, int p_argument_index) const;
		bool callable_type_from_method(const FSParser::DataType &p_receiver_type, const StringName &p_method_name, FSParser::Node *p_source, FSParser::DataType &r_callable_type);
		bool callable_type_from_constant_method_args(const FSParser::CallNode *p_call, int p_receiver_arg_index, int p_method_arg_index, FSParser::DataType &r_callable_type);
		bool call_argument_can_be_string_name(const FSParser::CallNode *p_call, int p_argument_index);
		void validate_strict_callable_method_fallback(const FSParser::CallNode *p_call, const FSParser::DataType &p_receiver_type, int p_method_arg_index);
		bool signal_name_from_constant_arg(const FSParser::CallNode *p_call, int p_signal_arg_index, StringName &r_signal_name) const;
		bool signal_type_from_receiver(const FSParser::DataType &p_receiver_type, const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const;
		bool signal_type_from_class_constant_arg(const FSParser::ClassNode *p_class, const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const;
		bool signal_type_from_native_constant_arg(const StringName &p_native_type, const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const;
		bool local_signal_type_from_constant_arg(const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const;
		void validate_strict_signal_name_fallback(const FSParser::CallNode *p_call, const FSParser::DataType &p_receiver_type, int p_signal_arg_index);
		void validate_call_arg(const List<FSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, const FSParser::CallNode *p_call, const Vector<int> &p_extra_allowed_argument_counts = Vector<int>(), int p_trailing_unbound_argument_count = 0);
		void validate_call_arg(const MethodInfo &p_method, const FSParser::CallNode *p_call);
		static bool call_has_named_arguments(const FSParser::CallNode *p_call);
		void reject_named_call_arguments(const FSParser::CallNode *p_call);
		bool canonicalize_named_call_arguments(FSParser::CallNode *p_call, const FSParser::FunctionNode *p_function);
		void validate_callable_array_literal_args(const Vector<FSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, FSParser::ArrayNode *p_array, const StringName &p_function, const Vector<int> &p_extra_allowed_argument_counts = Vector<int>(), int p_trailing_unbound_argument_count = 0);
		String make_invalid_argument_error(
				const StringName &p_function,
				int p_argument_number,
				const FSParser::DataType &p_expected_type,
				const FSParser::DataType &p_actual_type,
				bool p_strict_dynamic_mismatch,
				bool p_strict_nullable_mismatch,
				const FSParser::Node *p_actual_node = nullptr) const;
		void validate_signal_connect_arg(const FSParser::DataType &p_signal_type, const FSParser::CallNode *p_call, int p_callable_arg_index = 0, bool p_require_explicit_signal = true);
		void validate_signal_emit_args(const FSParser::DataType &p_signal_type, const FSParser::CallNode *p_call, int p_first_emit_arg_index);
		void validate_local_object_signal_callable_arg(const FSParser::CallNode *p_call, bool p_is_self);
		void validate_local_object_emit_signal_args(const FSParser::CallNode *p_call, bool p_is_self);
		void validate_typed_object_signal_api_args(const FSParser::DataType &p_base_type, const FSParser::CallNode *p_call, bool p_is_self);
	};

	CallSiteValidationContext call_site_validation;

	bool call_argument_is_same_receiver(const FSParser::CallNode *p_call, const FSParser::ExpressionNode *p_argument) const;
	bool datatype_contains_self_type_parameter(const FSParser::DataType &p_type) const;
	bool is_bare_self_value_parameter(const FSParser::DataType &p_type) const;
	bool datatype_matches_self_parameter_contract(const FSParser::DataType &p_expected_type, const FSParser::DataType &p_argument_type) const;
	String make_type_handle_argument_error(
			const StringName &p_function,
			int p_argument_number,
			const FSParser::DataType &p_expected_type,
			const FSParser::DataType &p_actual_type,
			const FSParser::Node *p_actual_node) const;
	void mark_coroutine_handle_capture(FSParser::ExpressionNode *p_expression, const FSParser::DataType &p_target_type);
	bool signature_type_involves_type_parameter(const FSParser::DataType &p_type) const;

	// Ensures deferred lambda bodies are resolved before leaving body analysis.
	class PendingLambdaBodiesScope {
		FSAnalyzer *analyzer = nullptr;

	public:
		explicit PendingLambdaBodiesScope(FSAnalyzer *p_analyzer);
		~PendingLambdaBodiesScope();
	};

	class AnalyzerPhaseScope {
		FSAnalyzer *analyzer = nullptr;
		AnalyzerPhase phase = AnalyzerPhase::NONE;
		bool completed = false;

	public:
		AnalyzerPhaseScope(FSAnalyzer *p_analyzer, AnalyzerPhase p_phase);
		~AnalyzerPhaseScope();
		void complete();
	};

	// Restores `parser->current_class` on scope exit during conformance target/type-argument
	// resolution and witness body analysis.
	class ScopedCurrentClass {
		FSAnalyzer *analyzer = nullptr;
		FSParser::ClassNode *previous_class = nullptr;

	public:
		ScopedCurrentClass(FSAnalyzer *p_analyzer, FSParser::ClassNode *p_current_class);
		~ScopedCurrentClass();
	};

	// A conformance witness resolves names against its target's member/type scope first and then falls
	// back to the lexical type scope of the file that declares the `extend`. Both halves are bound only
	// while a witness signature or body is analyzed, and both are restored on every exit path.
	class ScopedWitnessScope {
		FSAnalyzer *analyzer = nullptr;
		FSParser::ClassNode *previous_target = nullptr;
		FSParser::ClassNode *previous_declaration_scope = nullptr;

	public:
		ScopedWitnessScope(FSAnalyzer *p_analyzer, FSParser::ClassNode *p_target, FSParser::ClassNode *p_declaration_scope);
		~ScopedWitnessScope();
	};

	// The conformance target whose surface the witness currently under analysis binds to, and the root
	// class of the file that declares that conformance. Both are null outside witness analysis.
	FSParser::ClassNode *witness_target_class = nullptr;
	FSParser::ClassNode *witness_declaration_scope = nullptr;

	static const char *analyzer_phase_name(AnalyzerPhase p_phase);
	static AnalyzerPhase analyzer_phase_predecessor(AnalyzerPhase p_phase);
	void require_completed_analyzer_phase(AnalyzerPhase p_required_predecessor, AnalyzerPhase p_requested_phase) const;
	void mark_analyzer_phase_completed(AnalyzerPhase p_phase);
	String make_analyzer_phase_order_violation_message(AnalyzerPhase p_requested_phase, AnalyzerPhase p_required_predecessor) const;

	Error run_phase_preflight();
	Error run_phase_inheritance_resolution();
	Error run_phase_interface_and_member_surface();
	Error run_phase_trait_conformance_registration();
	Error run_phase_body_expression_callable_signal();
	void run_phase_flow_finality_invariants(FSParser::ClassNode *p_class);
	Error run_phase_conformance_witness_body();
	Error analyzer_result_from_parser_errors() const;
	Error run_phase_finalize_analyzer_warnings();
	Error run_phase_final_diagnostics_and_dependencies();
	void run_phase_apply_pending_warnings();

	const FSParser::EnumNode *current_enum = nullptr;
	FSParser::LambdaNode *current_lambda = nullptr;
	List<FSParser::LambdaNode *> pending_body_resolution_lambdas;
	bool static_context = false;
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
	bool resolving_function_signature_type = false;

	struct SuiteExitState {
		bool always_terminates = false;
		bool has_return = false;
		bool has_noreturn = false;
	};

	// Tests for detecting invalid overloading of script members
	static _FORCE_INLINE_ bool has_member_name_conflict_in_script_class(const StringName &p_name, const FSParser::ClassNode *p_current_class_node, const FSParser::Node *p_member);
	static _FORCE_INLINE_ bool has_member_name_conflict_in_native_type(const StringName &p_name, const StringName &p_native_type_string);
	Error check_native_member_name_conflict(const StringName &p_member_name, const FSParser::Node *p_member_node, const StringName &p_native_type_string);
	Error check_outer_class_member_name_conflict(const FSParser::ClassNode *p_class_node, const StringName &p_member_name, const FSParser::Node *p_member_node);
	Error check_class_member_name_conflict(const FSParser::ClassNode *p_class_node, const StringName &p_member_name, const FSParser::Node *p_member_node);

	void get_class_node_current_scope_classes(FSParser::ClassNode *p_node, List<FSParser::ClassNode *> *p_list, FSParser::Node *p_source);
	// Collects the lookup chain a name in the current scope actually sees: `p_node`'s own base/outer
	// chain first, then — while a conformance witness is being analyzed against `p_node` as its target —
	// the declaring file's lexical type scope, pointer-deduplicated. Classes reached only through the
	// fallback are reported in `r_declaration_site_classes`; they expose the type-bearing subset of an
	// ordinary outer-class surface, never values or callable members.
	void get_effective_scope_classes(FSParser::ClassNode *p_node, List<FSParser::ClassNode *> *p_list, FSParser::Node *p_source, HashSet<FSParser::ClassNode *> *r_declaration_site_classes = nullptr);
	static bool is_type_bearing_member(const FSParser::ClassNode::Member &p_member);
	bool declaration_site_class_declares_type(FSParser::ClassNode *p_class, const StringName &p_name, FSParser::Node *p_source);
	bool reduce_identifier_from_witness_declaration_scope(FSParser::IdentifierNode *p_identifier);
	bool witness_target_scope_declares_name(const StringName &p_name, const FSParser::Node *p_source);

	Error resolve_class_inheritance(FSParser::ClassNode *p_class, const FSParser::Node *p_source = nullptr);
	Error resolve_class_inheritance(FSParser::ClassNode *p_class, bool p_recursive);
	FSParser::DataType resolve_datatype(FSParser::TypeNode *p_type);
	bool resolve_type_parameter(const StringName &p_name, FSParser::DataType &r_type);
	FSParser::FunctionNode *find_generic_method(FSParser::ClassNode *p_class, const StringName &p_name, bool &r_found_member);
	FSParser::DataType substitute_member_type(
			const FSParser::DataType &p_member_type,
			const FSParser::DataType &p_base,
			const FSParser::FunctionNode *p_shadowing_method = nullptr,
			const FSParser::DataType *p_self_type = nullptr);
	bool apply_class_type_arguments(FSParser::DataType &r_type, const Vector<FSParser::TypeNode *> &p_argument_nodes, const FSParser::Node *p_source, bool p_check_bounds = true, Vector<bool> *r_argument_failed = nullptr);
	bool bind_class_type_arguments(FSParser::DataType &r_type, const Vector<FSParser::DataType> &p_arguments, const Vector<bool> &p_argument_failed, const Vector<const FSParser::Node *> &p_argument_sources, const FSParser::Node *p_source, bool p_check_bounds = true);
	bool check_class_type_argument_bounds(FSParser::DataType &r_type, const Vector<bool> &p_argument_failed, const Vector<const FSParser::Node *> &p_argument_sources);
	FSParser::DataType specialize_ancestor_type(const FSParser::DataType &p_base, const FSParser::ClassNode *p_target);

	void decide_suite_type(FSParser::Node *p_suite, FSParser::Node *p_statement);

	void resolve_annotation(FSParser::AnnotationNode *p_annotation, uint32_t p_target_kind = 0);
	void resolve_autoload_annotation(FSParser::AnnotationNode *p_annotation);
	void resolve_custom_annotation(FSParser::AnnotationNode *p_annotation, uint32_t p_target_kind);
	void resolve_annotation_declaration(FSParser::AnnotationDeclarationNode *p_declaration);
	FSParser::AnnotationDeclarationNode *resolve_custom_annotation_declaration(FSParser::AnnotationNode *p_annotation);
	FSParser::AnnotationDeclarationNode *resolve_qualified_annotation_declaration(const String &p_identity, FSParser::AnnotationNode *p_annotation);
	FSParser::AnnotationDeclarationNode *load_external_annotation_declaration(const String &p_qualified_name, FSParser::AnnotationNode *p_annotation, bool &r_error_reported);
	bool coerce_annotation_argument(const FSParser::DataType &p_parameter_type, Variant &r_value, const FSParser::ExpressionNode *p_argument, const String &p_context);
	bool get_autoload_dependency_name_from_expression(FSParser::ExpressionNode *p_expression, StringName &r_name);
	void resolve_class_member(FSParser::ClassNode *p_class, const StringName &p_name, const FSParser::Node *p_source = nullptr);
	void resolve_class_member(FSParser::ClassNode *p_class, int p_index, const FSParser::Node *p_source = nullptr);
	void resolve_function_signature_in_class(FSParser::FunctionNode *p_function,
			FSParser::ClassNode *p_class, const FSParser::Node *p_source);
	FSParser::DataType resolve_enum_values(FSParser::EnumNode *p_enum,
			const FSParser::DataType &p_enum_type, FSParser::ClassNode *p_owner);
	void resolve_enum_interface(FSParser::EnumNode *p_enum,
			const FSParser::DataType &p_enum_type, FSParser::ClassNode *p_owner);
	void resolve_enum_bodies(FSParser::EnumNode *p_enum, FSParser::ClassNode *p_owner);
	FSParser::DataType enum_self_type(const FSParser::FunctionNode *p_function) const;
	static FSParser::DataType complete_self_referential_enum_type(const FSParser::DataType &p_type);
	FSParser::EnumNode *resolve_enum_declaration(const FSParser::DataType &p_enum_type,
			const FSParser::Node *p_source);
	Error resolve_trait_uses(FSParser::ClassNode *p_class, const FSParser::Node *p_source = nullptr);
	Error resolve_trait_uses(FSParser::ClassNode *p_class, bool p_recursive);
	void resolve_class_interface(FSParser::ClassNode *p_class, const FSParser::Node *p_source = nullptr);
	void resolve_class_interface(FSParser::ClassNode *p_class, bool p_recursive);
	void resolve_class_body(FSParser::ClassNode *p_class, const FSParser::Node *p_source = nullptr);
	void resolve_class_body(FSParser::ClassNode *p_class, bool p_recursive);

	void resolve_function_signature(FSParser::FunctionNode *p_function, const FSParser::Node *p_source = nullptr, bool p_is_lambda = false);
	void resolve_function_body(FSParser::FunctionNode *p_function, bool p_is_lambda = false);
	void resolve_node(FSParser::Node *p_node, bool p_is_root = true);
	void resolve_suite(FSParser::SuiteNode *p_suite, bool p_is_root = true);
	SuiteExitState get_suite_exit_state(const FSParser::SuiteNode *p_suite) const;
	SuiteExitState get_statement_exit_state(const FSParser::Node *p_statement) const;
	bool suite_has_reachable_break(const FSParser::SuiteNode *p_suite) const;
	bool statement_has_reachable_break(const FSParser::Node *p_statement) const;
	void warn_unreachable_after_noreturn(const FSParser::SuiteNode *p_suite);
	void warn_unreachable_after_noreturn_in_statement(const FSParser::Node *p_statement);
	void resolve_assignable(FSParser::AssignableNode *p_assignable, const char *p_kind);
	void resolve_variable(FSParser::VariableNode *p_variable, bool p_is_local);
	void resolve_variable_destructure(FSParser::VariableDestructureNode *p_destructure);
	void resolve_constant(FSParser::ConstantNode *p_constant, bool p_is_local);
	void resolve_parameter(FSParser::ParameterNode *p_parameter);
	void resolve_if(FSParser::IfNode *p_if);
	void resolve_for(FSParser::ForNode *p_for);
	void resolve_while(FSParser::WhileNode *p_while);
	void resolve_assert(FSParser::AssertNode *p_assert);
	void resolve_match(FSParser::MatchNode *p_match);
#ifdef DEBUG_ENABLED
	void check_match_exhaustiveness(FSParser::MatchNode *p_match);
	void check_tagged_union_match_exhaustiveness(FSParser::MatchNode *p_match, const FSParser::DataType &p_match_type);
#endif
	void resolve_match_branch(FSParser::MatchBranchNode *p_match_branch, FSParser::ExpressionNode *p_match_test);
	void resolve_match_pattern(FSParser::PatternNode *p_match_pattern, FSParser::ExpressionNode *p_match_test, const FSParser::DataType *p_match_test_type = nullptr);
	void resolve_match_case_pattern(FSParser::PatternNode *p_match_pattern, const FSParser::DataType *p_match_test_type);
	void resolve_return(FSParser::ReturnNode *p_return);

	// Reduction functions.
	void reduce_expression(FSParser::ExpressionNode *p_expression, bool p_is_root = false);
	void reduce_array(FSParser::ArrayNode *p_array);
	void reduce_tuple_literal(FSParser::TupleLiteralNode *p_tuple_literal);
	void reduce_assignment(FSParser::AssignmentNode *p_assignment);
	void reduce_await(FSParser::AwaitNode *p_await);
	void reduce_binary_op(FSParser::BinaryOpNode *p_binary_op);
	void reduce_call(FSParser::CallNode *p_call, bool p_is_await = false, bool p_is_root = false);
	void reduce_cast(FSParser::CastNode *p_cast);
	void reduce_dictionary(FSParser::DictionaryNode *p_dictionary);
	void reduce_get_node(FSParser::GetNodeNode *p_get_node);
	void reduce_identifier(FSParser::IdentifierNode *p_identifier, bool can_be_builtin = false);
	void reduce_identifier_from_base(FSParser::IdentifierNode *p_identifier, FSParser::DataType *p_base = nullptr);
	void reduce_lambda(FSParser::LambdaNode *p_lambda);
	void reduce_literal(FSParser::LiteralNode *p_literal);
	void reduce_preload(FSParser::PreloadNode *p_preload);
	void reduce_self(FSParser::SelfNode *p_self);
	void reduce_subscript(FSParser::SubscriptNode *p_subscript, bool p_can_be_pseudo_type = false);
	void reduce_tuple_index_access(FSParser::SubscriptNode *p_subscript);
	void reduce_tuple_field_access(FSParser::SubscriptNode *p_subscript, const FSParser::DataType &p_base_type);
	bool find_global_tuple_meta_type(const StringName &p_name, FSParser::Node *p_source, FSParser::DataType &r_tuple_meta_type);
	bool find_named_tuple_meta_type(const FSParser::DataType &p_base_type, bool p_is_self, const StringName &p_name,
			const FSParser::Node *p_source, FSParser::DataType &r_tuple_meta_type);
	void reduce_call_tuple_construction(FSParser::CallNode *p_call, const FSParser::DataType &p_tuple_meta_type);
	void reduce_call_enum_case_construction(FSParser::CallNode *p_call, const FSParser::DataType &p_enum_meta_type);
	void reduce_ternary_op(FSParser::TernaryOpNode *p_ternary_op, bool p_is_root = false);
	void reduce_type_test(FSParser::TypeTestNode *p_type_test);
	void resolve_type_test_case_binds(FSParser::TypeTestNode *p_type_test, const FSParser::DataType &p_test_type);
	void reduce_unary_op(FSParser::UnaryOpNode *p_unary_op);

	Variant make_expression_reduced_value(FSParser::ExpressionNode *p_expression, bool &is_reduced);
	Variant make_array_reduced_value(FSParser::ArrayNode *p_array, bool &is_reduced);
	Variant make_tuple_literal_reduced_value(FSParser::TupleLiteralNode *p_tuple_literal, bool &is_reduced);
	Variant make_dictionary_reduced_value(FSParser::DictionaryNode *p_dictionary, bool &is_reduced);
	Variant make_subscript_reduced_value(FSParser::SubscriptNode *p_subscript, bool &is_reduced);
	Variant make_call_reduced_value(FSParser::CallNode *p_call, bool &is_reduced);

	// Helpers.
	// Builds a TUPLE DataType. `p_tuple_name` is empty for an unnamed (structural) tuple; a named
	// tuple additionally carries the declaring script path so nominal identity survives imports.
	static FSParser::DataType make_tuple_type(const StringName &p_tuple_name, const String &p_owner_fqcn, const String &p_script_path,
			const Vector<FSParser::DataType> &p_element_types, const Vector<StringName> &p_field_names, bool p_meta);
	Array make_array_from_element_datatype(const FSParser::DataType &p_element_datatype, const FSParser::Node *p_source_node = nullptr);
	Dictionary make_dictionary_from_element_datatype(const FSParser::DataType &p_key_element_datatype, const FSParser::DataType &p_value_element_datatype, const FSParser::Node *p_source_node = nullptr);
	ContainerType make_container_type_from_datatype(const FSParser::DataType &p_datatype, const FSParser::Node *p_source_node);
	FSParser::DataType type_from_variant(const Variant &p_value, const FSParser::Node *p_source);
	FSParser::DataType type_from_property(const PropertyInfo &p_property, bool p_is_arg = false, bool p_is_readonly = false) const;
	bool is_bootstrap_dependency_path_allowed(const String &p_path) const;
	bool validate_bootstrap_namespace_import(const String &p_import, const LocalVector<StringName> &p_global_classes);
	bool reject_bootstrap_global_class_dependency(const StringName &p_class_name, const FSParser::Node *p_source, const String &p_context);
	FSParser::DataType make_global_class_meta_type(const StringName &p_class_name, const FSParser::Node *p_source);
	FSParser::DataType make_global_tuple_type_from_current_parser(const StringName &p_global_name, const FSParser::Node *p_source);
	FSParser::DataType make_global_enum_type_from_path(
			const StringName &p_global_name, const String &p_path, const FSParser::Node *p_source);
	FSParser::DataType make_global_enum_type_from_current_parser(
			const StringName &p_global_name, const FSParser::Node *p_source);
	uint32_t get_autoload_settings_hash() const;
	void ensure_autoload_index_current();
	bool get_autoload_singleton_value_type(const StringName &p_name, FSParser::DataType &r_type);
	bool get_global_class_in_namespace(const String &p_namespace, const StringName &p_class_name, StringName &r_global_class_name) const;
	bool get_imported_global_class(const StringName &p_class_name, const FSParser::Node *p_source, StringName &r_global_class_name, bool &r_error, const String &p_symbol_kind = "type");
	bool get_namespace_global_class_from_type_chain(const Vector<FSParser::IdentifierNode *> &p_type_chain, const FSParser::Node *p_source, StringName &r_global_class_name, int &r_type_chain_size, bool &r_error, const String &p_symbol_kind = "type");
	bool is_namespace_chain_root_shadowed(FSParser::IdentifierNode *p_identifier);
	FSParser::ClassNode *resolve_trait_reference(FSParser::ClassNode *p_owner,
			FSParser::ClassNode::TraitUse &r_trait_use, const FSParser::Node *p_source);
	FSParser::ClassNode *resolve_local_trait_reference(FSParser::ClassNode *p_owner,
			const FSParser::ClassNode::TraitUse &p_trait_use, const FSParser::Node *p_source,
			bool &r_found);
	FSParser::ClassNode *resolve_global_trait_reference(const StringName &p_global_class_name,
			const FSParser::Node *p_source);
	FSParser::ClassNode *resolve_nested_trait_reference(FSParser::ClassNode *p_base,
			const FSParser::ClassNode::TraitUse &p_trait_use, int p_chain_index,
			const FSParser::Node *p_source);
	bool class_satisfies_trait_base(FSParser::ClassNode *p_class, FSParser::ClassNode *p_trait);
	bool datatype_derives_from_datatype(FSParser::DataType p_type, const FSParser::DataType &p_base);
	bool type_argument_satisfies_bound(const FSParser::DataType &p_argument, const FSParser::DataType &p_bound);
	bool type_satisfies_trait(const FSParser::DataType &p_argument, const FSParser::DataType &p_trait_bound);
	void validate_trait_conflicts(FSParser::ClassNode *p_class);
	void validate_trait_requirements(FSParser::ClassNode *p_class);
	void raise_declared_conformance_dependencies();
	void resolve_conformances(FSParser::ClassNode *p_class);
	void resolve_conformance_bodies(FSParser::ClassNode *p_class);
	FSParser::ClassNode *resolve_conformance_target(FSParser::ConformanceNode *p_conformance, FSParser::DataType &r_target_type);
	FSParser::ClassNode *resolve_native_conformance_shim(FSParser::ConformanceNode *p_conformance, const FSParser::DataType &p_native_type);
	FSParser::ClassNode *resolve_builtin_conformance_shim(FSParser::ConformanceNode *p_conformance, const FSParser::DataType &p_builtin_type);
	bool validate_conformance(FSParser::ConformanceNode *p_conformance, FSParser::ClassNode *p_target,
			FSParser::ClassNode *p_trait, const HashMap<StringName, FSParser::DataType> &p_trait_substitution);
	HashMap<StringName, FSParser::DataType> conformance_trait_substitution(FSParser::ClassNode *p_trait,
			const FSParser::ClassNode::TraitUse &p_trait_use);
	FSParser::FunctionNode *find_static_conformance_witness(const FSParser::DataType &p_target_type, const StringName &p_method);
	// True once this analysis has run the conformance-index probe, so it runs at most once per file.
	bool indexed_conformance_files_probed = false;
	// Registers the conformances of every conformance-declaring file the project-wide declaration
	// index knows about, so the hidden-witness diagnostic reflects the project on disk instead of
	// whatever this process happened to analyze first. Creates no reach.
	void ensure_indexed_conformance_files_registered();
	// True when a retroactive conformance this file can reach supplies `p_method` anywhere on
	// `p_target_type`'s base chain. Only the name is checked: instance witnesses are not resolved
	// statically, so this answers "could this call mean something" and never types it.
	bool reachable_conformance_supplies_method(const FSParser::DataType &p_target_type, const StringName &p_method);
	// True when a retroactive conformance supplies `p_method` for `p_target_type` but this file does
	// not load its declaring file, reporting where it was declared. Lets an unresolved call say why
	// it cannot be typed instead of passing analysis and missing at run time.
	bool find_hidden_conformance_witness(const FSParser::DataType &p_target_type, const StringName &p_method,
			String &r_source_file, StringName &r_trait_name);
	bool find_trait_implementation(FSParser::ClassNode *p_class, const StringName &p_function_name,
			TraitMethodImplementation &r_implementation);
	HashMap<StringName, FSParser::DataType> trait_type_argument_substitution(FSParser::ClassNode *p_class, FSParser::ClassNode *p_trait);
	bool validate_trait_method_signature(FSParser::ClassNode *p_trait,
			FSParser::ClassNode *p_implementing_class, FSParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation,
			const HashMap<StringName, FSParser::DataType> &p_trait_substitution = HashMap<StringName, FSParser::DataType>());
	bool validate_trait_method_info_signature(FSParser::ClassNode *p_trait,
			FSParser::ClassNode *p_implementing_class, FSParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation,
			const HashMap<StringName, FSParser::DataType> &p_trait_substitution = HashMap<StringName, FSParser::DataType>());
	Error validate_imports();
	Error validate_annotation_declarations();
	void resolve_annotation_declaration_signatures();
#ifdef DEBUG_ENABLED
	void validate_mixed_namespace_directory();
#endif // DEBUG_ENABLED
	bool get_function_signature(FSParser::Node *p_source, bool p_is_constructor,
			FSParser::DataType base_type, const StringName &p_function,
			FSParser::DataType &r_return_type, List<FSParser::DataType> &r_par_types,
			int &r_default_arg_count, BitField<MethodFlags> &r_method_flags,
			StringName *r_native_class = nullptr, bool *r_is_noreturn = nullptr,
			FSParser::FunctionNode **r_found_function = nullptr,
			FSParser::ClassNode **r_found_in_class = nullptr,
			const FSParser::DataType *p_self_type_override = nullptr);
	bool apply_builtin_native_return_type_hint(const StringName &p_native_method_owner, const StringName &p_method,
			const FSParser::Node *p_source, FSParser::DataType &r_return_type);
	bool resolve_explicit_type_argument(FSParser::ExpressionNode *p_expression, FSParser::DataType &r_type_argument);
	void apply_use_site_nullable_type_argument_marker(FSParser::DataType &r_type_argument, bool p_is_nullable);
	void reduce_call_create_proxy(FSParser::CallNode *p_call, FSParser::SubscriptNode *p_callee);
	bool function_signature_from_info(const MethodInfo &p_info, FSParser::DataType &r_return_type, List<FSParser::DataType> &r_par_types, int &r_default_arg_count, BitField<MethodFlags> &r_method_flags);
	bool string_name_from_constant_arg(const FSParser::CallNode *p_call, int p_argument_index, StringName &r_name) const;
	bool is_node_compatible_type(const FSParser::DataType &p_type) const;
	bool property_type_from_class(FSParser::ClassNode *p_class, const StringName &p_property_name, FSParser::Node *p_source, FSParser::DataType &r_property_type);
	bool property_type_from_script(const Ref<Script> &p_script, const StringName &p_property_name, FSParser::DataType &r_property_type) const;
	bool property_type_from_native(const StringName &p_native_type, const StringName &p_property_name, FSParser::DataType &r_property_type) const;
	bool property_type_from_receiver(const FSParser::DataType &p_receiver_type, const StringName &p_property_name, FSParser::Node *p_source, FSParser::DataType &r_property_type);
	bool property_path_from_constant_arg(const FSParser::CallNode *p_call, int p_argument_index, Vector<StringName> &r_property_path) const;
	bool property_type_from_builtin_member(const FSParser::DataType &p_base_type, const StringName &p_member_name, FSParser::DataType &r_member_type) const;
	bool property_type_from_indexed_receiver(const FSParser::DataType &p_receiver_type, const Vector<StringName> &p_property_path, FSParser::Node *p_source, FSParser::DataType &r_property_type);
	FSParser::DataType get_operation_type(Variant::Operator p_operation, const FSParser::DataType &p_a, const FSParser::DataType &p_b, bool &r_valid, const FSParser::Node *p_source);
	FSParser::DataType get_operation_type(Variant::Operator p_operation, const FSParser::DataType &p_a, bool &r_valid, const FSParser::Node *p_source);
	void update_const_expression_builtin_type(FSParser::ExpressionNode *p_expression, const FSParser::DataType &p_type, const char *p_usage, bool p_is_cast = false);
	void update_array_literal_element_type(FSParser::ArrayNode *p_array,
			const FSParser::DataType &p_element_type,
			bool p_self_parameter_contract = false,
			bool p_substitute_self_runtime_type = false);
	void update_dictionary_literal_element_type(FSParser::DictionaryNode *p_dictionary,
			const FSParser::DataType &p_key_element_type,
			const FSParser::DataType &p_value_element_type,
			bool p_self_parameter_contract = false,
			bool p_substitute_self_runtime_type = false);
	bool is_type_compatible(const FSParser::DataType &p_target, const FSParser::DataType &p_source, bool p_allow_implicit_conversion = false, const FSParser::Node *p_source_node = nullptr);
	void push_error(const String &p_message, const FSParser::Node *p_origin = nullptr);
	void mark_node_unsafe(const FSParser::Node *p_node);
	void downgrade_node_type_source(FSParser::Node *p_node);
	void mark_lambda_use_self();
	void resolve_pending_lambda_bodies();
	void reduce_identifier_from_base_set_class(FSParser::IdentifierNode *p_identifier, FSParser::DataType p_identifier_datatype);
	Ref<FoundryScript> get_depended_shallow_script(const String &p_path, Error &r_error);
#ifdef DEBUG_ENABLED
	void is_shadowing(FSParser::IdentifierNode *p_identifier, const String &p_context, const bool p_in_local_scope);
#endif

public:
	Error resolve_inheritance();
	Error resolve_interface();
	Error resolve_body();
	Error resolve_dependencies();
	Error analyze();
	void set_strict_null_checks(bool p_enabled) { strict_null_checks = p_enabled; }
	void set_strict_dynamic_checks(bool p_enabled) { strict_dynamic_checks = p_enabled; }
	const FSAutoloadIndex &get_autoload_index() const { return autoload_index; }

	Variant make_variable_default_value(FSParser::VariableNode *p_variable);

	static bool check_type_compatibility(const FSParser::DataType &p_target, const FSParser::DataType &p_source, bool p_allow_implicit_conversion = false, const FSParser::Node *p_source_node = nullptr);
	static FSParser::DataType type_from_metatype(const FSParser::DataType &p_meta_type);
	static bool class_exists(const StringName &p_class);
	static void set_bootstrap_allowed_dependency_root(const String &p_root);
	static String get_bootstrap_allowed_dependency_root();
	// Whether `p_path` is inside the build-task bootstrap root active on this thread, or true when no
	// bootstrap is in progress. The static form of `is_bootstrap_dependency_path_allowed`, for callers
	// outside an analysis that must respect the same boundary.
	static bool is_bootstrap_path_allowed(const String &p_path);

	FSAnalyzer(FSParser *p_parser);

#ifdef TESTS_ENABLED
	String test_format_phase_order_violation(AnalyzerPhase p_requested_phase, AnalyzerPhase p_required_predecessor) const;
	bool test_would_violate_phase_order(AnalyzerPhase p_requested_phase, AnalyzerPhase p_required_predecessor) const;
	AnalyzerPhase test_get_highest_completed_phase() const { return highest_completed_phase; }
	void test_mark_analyzer_phase_completed(AnalyzerPhase p_phase) { mark_analyzer_phase_completed(p_phase); }
	static FSParserRef::Status test_get_depended_parser_status(const FSAnalyzer *p_analyzer, const String &p_path);
	static Ref<FSParserRef> test_get_depended_parser_ref(const FSAnalyzer *p_analyzer, const String &p_path);
	static int test_get_external_parser_cache_size(const FSAnalyzer *p_analyzer);
#endif // TESTS_ENABLED

#ifdef TESTS_ENABLED
	// Grants unit tests access to the private PropertyInfo decode path so the encode/decode round-trip
	// of typed callable/signal signatures can be exercised directly (see test_foundry_script_type.h).
	friend class FSTests::TestFSAnalyzerAccessor;
	friend class FSTests::TestFSAnalyzerDependencyAccessAccessor;
#endif // TESTS_ENABLED
};
