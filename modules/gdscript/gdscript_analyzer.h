/**************************************************************************/
/*  gdscript_analyzer.h                                                   */
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

#pragma once

#include "gdscript_cache.h"
#include "gdscript_parser.h"

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/variant/container_type_validate.h"

class GDScriptAnalyzer {
	GDScriptParser *parser = nullptr;

	struct TraitMethodImplementation {
		GDScriptParser::FunctionNode *function = nullptr;
		GDScriptParser::ClassNode *owner_class = nullptr;
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

	const GDScriptParser::EnumNode *current_enum = nullptr;
	GDScriptParser::LambdaNode *current_lambda = nullptr;
	List<GDScriptParser::LambdaNode *> pending_body_resolution_lambdas;
	HashMap<const GDScriptParser::ClassNode *, Ref<GDScriptParserRef>> external_class_parser_cache;
	HashMap<const GDScriptParser::Node *, GDScriptParser::DataType> flow_narrowed_types;
	HashMap<const GDScriptParser::Node *, bool> flow_narrowing_captured_sources;
	bool static_context = false;
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;

	struct SuiteExitState {
		bool always_terminates = false;
		bool has_return = false;
		bool has_noreturn = false;
	};

	// Tests for detecting invalid overloading of script members
	static _FORCE_INLINE_ bool has_member_name_conflict_in_script_class(const StringName &p_name, const GDScriptParser::ClassNode *p_current_class_node, const GDScriptParser::Node *p_member);
	static _FORCE_INLINE_ bool has_member_name_conflict_in_native_type(const StringName &p_name, const StringName &p_native_type_string);
	Error check_native_member_name_conflict(const StringName &p_member_name, const GDScriptParser::Node *p_member_node, const StringName &p_native_type_string);
	Error check_class_member_name_conflict(const GDScriptParser::ClassNode *p_class_node, const StringName &p_member_name, const GDScriptParser::Node *p_member_node);

	void get_class_node_current_scope_classes(GDScriptParser::ClassNode *p_node, List<GDScriptParser::ClassNode *> *p_list, GDScriptParser::Node *p_source);

	Error resolve_class_inheritance(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source = nullptr);
	Error resolve_class_inheritance(GDScriptParser::ClassNode *p_class, bool p_recursive);
	GDScriptParser::DataType resolve_datatype(GDScriptParser::TypeNode *p_type);
	bool resolve_type_parameter(const StringName &p_name, GDScriptParser::DataType &r_type);
	GDScriptParser::DataType substitute_member_type(const GDScriptParser::DataType &p_member_type, const GDScriptParser::DataType &p_base, const GDScriptParser::FunctionNode *p_shadowing_method = nullptr);
	bool apply_class_type_arguments(GDScriptParser::DataType &r_type, const Vector<GDScriptParser::TypeNode *> &p_argument_nodes, const GDScriptParser::Node *p_source);
	bool bind_class_type_arguments(GDScriptParser::DataType &r_type, const Vector<GDScriptParser::DataType> &p_arguments, const Vector<bool> &p_argument_failed, const Vector<const GDScriptParser::Node *> &p_argument_sources, const GDScriptParser::Node *p_source);
	GDScriptParser::DataType specialize_ancestor_type(const GDScriptParser::DataType &p_base, const GDScriptParser::ClassNode *p_target);

	void decide_suite_type(GDScriptParser::Node *p_suite, GDScriptParser::Node *p_statement);

	void resolve_annotation(GDScriptParser::AnnotationNode *p_annotation);
	void resolve_class_member(GDScriptParser::ClassNode *p_class, const StringName &p_name, const GDScriptParser::Node *p_source = nullptr);
	void resolve_class_member(GDScriptParser::ClassNode *p_class, int p_index, const GDScriptParser::Node *p_source = nullptr);
	void resolve_function_signature_in_class(GDScriptParser::FunctionNode *p_function,
			GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source);
	Error resolve_trait_uses(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source = nullptr);
	Error resolve_trait_uses(GDScriptParser::ClassNode *p_class, bool p_recursive);
	void resolve_class_interface(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source = nullptr);
	void resolve_class_interface(GDScriptParser::ClassNode *p_class, bool p_recursive);
	void resolve_class_body(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source = nullptr);
	void resolve_class_body(GDScriptParser::ClassNode *p_class, bool p_recursive);
	void resolve_function_signature(GDScriptParser::FunctionNode *p_function, const GDScriptParser::Node *p_source = nullptr, bool p_is_lambda = false);
	void resolve_function_body(GDScriptParser::FunctionNode *p_function, bool p_is_lambda = false);
	void resolve_node(GDScriptParser::Node *p_node, bool p_is_root = true);
	void resolve_suite(GDScriptParser::SuiteNode *p_suite, bool p_is_root = true);
	SuiteExitState get_suite_exit_state(const GDScriptParser::SuiteNode *p_suite) const;
	SuiteExitState get_statement_exit_state(const GDScriptParser::Node *p_statement) const;
	bool suite_has_reachable_break(const GDScriptParser::SuiteNode *p_suite) const;
	bool statement_has_reachable_break(const GDScriptParser::Node *p_statement) const;
	void warn_unreachable_after_noreturn(const GDScriptParser::SuiteNode *p_suite);
	void warn_unreachable_after_noreturn_in_statement(const GDScriptParser::Node *p_statement);
	void resolve_assignable(GDScriptParser::AssignableNode *p_assignable, const char *p_kind);
	void resolve_variable(GDScriptParser::VariableNode *p_variable, bool p_is_local);
	void resolve_constant(GDScriptParser::ConstantNode *p_constant, bool p_is_local);
	void resolve_parameter(GDScriptParser::ParameterNode *p_parameter);
	void resolve_if(GDScriptParser::IfNode *p_if);
	void resolve_for(GDScriptParser::ForNode *p_for);
	void resolve_while(GDScriptParser::WhileNode *p_while);
	void resolve_assert(GDScriptParser::AssertNode *p_assert);
	void resolve_match(GDScriptParser::MatchNode *p_match);
#ifdef DEBUG_ENABLED
	void check_match_exhaustiveness(GDScriptParser::MatchNode *p_match);
#endif
	void resolve_match_branch(GDScriptParser::MatchBranchNode *p_match_branch, GDScriptParser::ExpressionNode *p_match_test);
	void resolve_match_pattern(GDScriptParser::PatternNode *p_match_pattern, GDScriptParser::ExpressionNode *p_match_test, const GDScriptParser::DataType *p_match_test_type = nullptr);
	void resolve_return(GDScriptParser::ReturnNode *p_return);

	// Reduction functions.
	void reduce_expression(GDScriptParser::ExpressionNode *p_expression, bool p_is_root = false);
	void reduce_array(GDScriptParser::ArrayNode *p_array);
	void reduce_assignment(GDScriptParser::AssignmentNode *p_assignment);
	void reduce_await(GDScriptParser::AwaitNode *p_await);
	void reduce_binary_op(GDScriptParser::BinaryOpNode *p_binary_op);
	void reduce_call(GDScriptParser::CallNode *p_call, bool p_is_await = false, bool p_is_root = false);
	void reduce_cast(GDScriptParser::CastNode *p_cast);
	void reduce_dictionary(GDScriptParser::DictionaryNode *p_dictionary);
	void reduce_get_node(GDScriptParser::GetNodeNode *p_get_node);
	void reduce_identifier(GDScriptParser::IdentifierNode *p_identifier, bool can_be_builtin = false);
	void reduce_identifier_from_base(GDScriptParser::IdentifierNode *p_identifier, GDScriptParser::DataType *p_base = nullptr);
	void reduce_lambda(GDScriptParser::LambdaNode *p_lambda);
	void reduce_literal(GDScriptParser::LiteralNode *p_literal);
	void reduce_preload(GDScriptParser::PreloadNode *p_preload);
	void reduce_self(GDScriptParser::SelfNode *p_self);
	void reduce_subscript(GDScriptParser::SubscriptNode *p_subscript, bool p_can_be_pseudo_type = false);
	void reduce_ternary_op(GDScriptParser::TernaryOpNode *p_ternary_op, bool p_is_root = false);
	void reduce_type_test(GDScriptParser::TypeTestNode *p_type_test);
	void reduce_unary_op(GDScriptParser::UnaryOpNode *p_unary_op);

	Variant make_expression_reduced_value(GDScriptParser::ExpressionNode *p_expression, bool &is_reduced);
	Variant make_array_reduced_value(GDScriptParser::ArrayNode *p_array, bool &is_reduced);
	Variant make_dictionary_reduced_value(GDScriptParser::DictionaryNode *p_dictionary, bool &is_reduced);
	Variant make_subscript_reduced_value(GDScriptParser::SubscriptNode *p_subscript, bool &is_reduced);
	Variant make_call_reduced_value(GDScriptParser::CallNode *p_call, bool &is_reduced);

	// Helpers.
	Array make_array_from_element_datatype(const GDScriptParser::DataType &p_element_datatype, const GDScriptParser::Node *p_source_node = nullptr);
	Dictionary make_dictionary_from_element_datatype(const GDScriptParser::DataType &p_key_element_datatype, const GDScriptParser::DataType &p_value_element_datatype, const GDScriptParser::Node *p_source_node = nullptr);
	ContainerType make_container_type_from_datatype(const GDScriptParser::DataType &p_datatype, const GDScriptParser::Node *p_source_node);
	GDScriptParser::DataType type_from_variant(const Variant &p_value, const GDScriptParser::Node *p_source);
	GDScriptParser::DataType type_from_property(const PropertyInfo &p_property, bool p_is_arg = false, bool p_is_readonly = false) const;
	GDScriptParser::DataType make_global_class_meta_type(const StringName &p_class_name, const GDScriptParser::Node *p_source);
	bool get_global_class_in_namespace(const String &p_namespace, const StringName &p_class_name, StringName &r_global_class_name) const;
	bool get_imported_global_class(const StringName &p_class_name, const GDScriptParser::Node *p_source, StringName &r_global_class_name, bool &r_error, const String &p_symbol_kind = "type");
	bool get_namespace_global_class_from_type_chain(const Vector<GDScriptParser::IdentifierNode *> &p_type_chain, const GDScriptParser::Node *p_source, StringName &r_global_class_name, int &r_type_chain_size, bool &r_error, const String &p_symbol_kind = "type");
	bool is_namespace_chain_root_shadowed(GDScriptParser::IdentifierNode *p_identifier);
	GDScriptParser::ClassNode *resolve_trait_reference(GDScriptParser::ClassNode *p_owner,
			GDScriptParser::ClassNode::TraitUse &r_trait_use, const GDScriptParser::Node *p_source);
	GDScriptParser::ClassNode *resolve_local_trait_reference(GDScriptParser::ClassNode *p_owner,
			const GDScriptParser::ClassNode::TraitUse &p_trait_use, const GDScriptParser::Node *p_source,
			bool &r_found);
	GDScriptParser::ClassNode *resolve_global_trait_reference(const StringName &p_global_class_name,
			const GDScriptParser::Node *p_source);
	GDScriptParser::ClassNode *resolve_nested_trait_reference(GDScriptParser::ClassNode *p_base,
			const GDScriptParser::ClassNode::TraitUse &p_trait_use, int p_chain_index,
			const GDScriptParser::Node *p_source);
	bool class_satisfies_trait_base(GDScriptParser::ClassNode *p_class, GDScriptParser::ClassNode *p_trait);
	bool datatype_derives_from_datatype(GDScriptParser::DataType p_type, const GDScriptParser::DataType &p_base);
	bool type_argument_satisfies_bound(const GDScriptParser::DataType &p_argument, const GDScriptParser::DataType &p_bound);
	bool type_satisfies_trait(const GDScriptParser::DataType &p_argument, const GDScriptParser::DataType &p_trait_bound);
	void validate_trait_conflicts(GDScriptParser::ClassNode *p_class);
	void validate_trait_requirements(GDScriptParser::ClassNode *p_class);
	bool find_trait_implementation(GDScriptParser::ClassNode *p_class, const StringName &p_function_name,
			TraitMethodImplementation &r_implementation);
	bool validate_trait_method_signature(GDScriptParser::ClassNode *p_trait,
			GDScriptParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation);
	bool validate_trait_method_info_signature(GDScriptParser::ClassNode *p_trait,
			GDScriptParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation);
	Error validate_imports();
#ifdef DEBUG_ENABLED
	void validate_mixed_namespace_directory();
#endif // DEBUG_ENABLED
	bool get_function_signature(GDScriptParser::Node *p_source, bool p_is_constructor,
			GDScriptParser::DataType base_type, const StringName &p_function,
			GDScriptParser::DataType &r_return_type, List<GDScriptParser::DataType> &r_par_types,
			int &r_default_arg_count, BitField<MethodFlags> &r_method_flags,
			StringName *r_native_class = nullptr, bool *r_is_noreturn = nullptr,
			GDScriptParser::FunctionNode **r_found_function = nullptr);
	void collect_type_parameter_bindings(const GDScriptParser::DataType &p_parameter_type, const GDScriptParser::DataType &p_argument_type,
			HashMap<StringName, GDScriptParser::DataType> &r_bindings, HashSet<StringName> &r_conflicts);
	bool merge_inferred_type_argument(const GDScriptParser::DataType &p_existing, const GDScriptParser::DataType &p_candidate, GDScriptParser::DataType &r_merged);
	bool resolve_explicit_type_argument(GDScriptParser::ExpressionNode *p_expression, GDScriptParser::DataType &r_type_argument);
	void apply_generic_method_call(GDScriptParser::CallNode *p_call, GDScriptParser::FunctionNode *p_function,
			List<GDScriptParser::DataType> &r_par_types, GDScriptParser::DataType &r_return_type);
	void reduce_call_create_proxy(GDScriptParser::CallNode *p_call, GDScriptParser::SubscriptNode *p_callee);
	bool function_signature_from_info(const MethodInfo &p_info, GDScriptParser::DataType &r_return_type, List<GDScriptParser::DataType> &r_par_types, int &r_default_arg_count, BitField<MethodFlags> &r_method_flags);
	bool callable_signature_from_type(const GDScriptParser::DataType &p_callable_type, Vector<GDScriptParser::DataType> &r_par_types, int &r_default_arg_count, bool &r_is_vararg) const;
	GDScriptParser::DataType plain_callable_type() const;
	GDScriptParser::DataType explicit_callable_type_from_signature(const GDScriptParser::DataType &p_return_type, const Vector<GDScriptParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const;
	GDScriptParser::DataType transformed_callable_type(const GDScriptParser::DataType &p_source_callable_type, const Vector<GDScriptParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const;
	GDScriptParser::DataType explicit_callable_type_from_info(const MethodInfo &p_info) const;
	GDScriptParser::DataType explicit_signal_type_from_info(const MethodInfo &p_info) const;
	GDScriptParser::DataType explicit_signal_type_from_node(const GDScriptParser::SignalNode *p_signal) const;
	GDScriptParser::ArrayNode *array_literal_argument(const GDScriptParser::CallNode *p_call, int p_argument_index) const;
	bool string_name_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_argument_index, StringName &r_name) const;
	bool call_argument_can_be_string_name(const GDScriptParser::CallNode *p_call, int p_argument_index);
	bool callable_type_from_method(const GDScriptParser::DataType &p_receiver_type, const StringName &p_method_name, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_callable_type);
	bool callable_type_from_constant_method_args(const GDScriptParser::CallNode *p_call, int p_receiver_arg_index, int p_method_arg_index, GDScriptParser::DataType &r_callable_type);
	void validate_strict_callable_method_fallback(const GDScriptParser::CallNode *p_call, const GDScriptParser::DataType &p_receiver_type, int p_method_arg_index);
	bool is_node_compatible_type(const GDScriptParser::DataType &p_type) const;
	bool property_type_from_class(GDScriptParser::ClassNode *p_class, const StringName &p_property_name, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_property_type);
	bool property_type_from_script(const Ref<Script> &p_script, const StringName &p_property_name, GDScriptParser::DataType &r_property_type) const;
	bool property_type_from_native(const StringName &p_native_type, const StringName &p_property_name, GDScriptParser::DataType &r_property_type) const;
	bool property_type_from_receiver(const GDScriptParser::DataType &p_receiver_type, const StringName &p_property_name, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_property_type);
	bool property_path_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_argument_index, Vector<StringName> &r_property_path) const;
	bool property_type_from_builtin_member(const GDScriptParser::DataType &p_base_type, const StringName &p_member_name, GDScriptParser::DataType &r_member_type) const;
	bool property_type_from_indexed_receiver(const GDScriptParser::DataType &p_receiver_type, const Vector<StringName> &p_property_path, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_property_type);
	bool signal_name_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_signal_arg_index, StringName &r_signal_name) const;
	bool signal_type_from_receiver(const GDScriptParser::DataType &p_receiver_type, const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const;
	bool signal_type_from_class_constant_arg(const GDScriptParser::ClassNode *p_class, const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const;
	bool signal_type_from_native_constant_arg(const StringName &p_native_type, const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const;
	bool local_signal_type_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const;
	void validate_strict_signal_name_fallback(const GDScriptParser::CallNode *p_call, const GDScriptParser::DataType &p_receiver_type, int p_signal_arg_index);
	const GDScriptParser::Node *flow_narrowing_key_from_identifier(const GDScriptParser::IdentifierNode *p_identifier) const;
	void apply_flow_narrowing(const GDScriptParser::IdentifierNode *p_identifier);
	void apply_flow_narrowing(const GDScriptParser::IdentifierNode *p_identifier, const GDScriptParser::DataType &p_type);
	void clear_flow_narrowing(const GDScriptParser::ExpressionNode *p_expression);
	void mark_flow_narrowing_capture(const GDScriptParser::IdentifierNode *p_identifier);
	void clear_captured_flow_narrowing();
	bool null_check_narrowing_identifier(GDScriptParser::ExpressionNode *p_condition, bool p_condition_value, GDScriptParser::IdentifierNode *&r_identifier) const;
	bool type_test_narrowing_identifier(GDScriptParser::ExpressionNode *p_condition, bool p_condition_value, GDScriptParser::IdentifierNode *&r_identifier, GDScriptParser::DataType &r_type) const;
	void validate_call_arg(const List<GDScriptParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, const GDScriptParser::CallNode *p_call, const Vector<int> &p_extra_allowed_argument_counts = Vector<int>(), int p_trailing_unbound_argument_count = 0);
	void validate_call_arg(const MethodInfo &p_method, const GDScriptParser::CallNode *p_call);
	void validate_callable_array_literal_args(const Vector<GDScriptParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, GDScriptParser::ArrayNode *p_array, const StringName &p_function, const Vector<int> &p_extra_allowed_argument_counts = Vector<int>(), int p_trailing_unbound_argument_count = 0);
	String make_invalid_argument_error(const StringName &p_function, int p_argument_number, const GDScriptParser::DataType &p_expected_type, const GDScriptParser::DataType &p_actual_type, bool p_strict_dynamic_mismatch, bool p_strict_nullable_mismatch) const;
	void validate_signal_connect_arg(const GDScriptParser::DataType &p_signal_type, const GDScriptParser::CallNode *p_call, int p_callable_arg_index = 0, bool p_require_explicit_signal = true);
	void validate_signal_emit_args(const GDScriptParser::DataType &p_signal_type, const GDScriptParser::CallNode *p_call, int p_first_emit_arg_index);
	void validate_local_object_signal_callable_arg(const GDScriptParser::CallNode *p_call, bool p_is_self);
	void validate_local_object_emit_signal_args(const GDScriptParser::CallNode *p_call, bool p_is_self);
	void validate_typed_object_signal_api_args(const GDScriptParser::DataType &p_base_type, const GDScriptParser::CallNode *p_call, bool p_is_self);
	GDScriptParser::DataType get_operation_type(Variant::Operator p_operation, const GDScriptParser::DataType &p_a, const GDScriptParser::DataType &p_b, bool &r_valid, const GDScriptParser::Node *p_source);
	GDScriptParser::DataType get_operation_type(Variant::Operator p_operation, const GDScriptParser::DataType &p_a, bool &r_valid, const GDScriptParser::Node *p_source);
	void update_const_expression_builtin_type(GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::DataType &p_type, const char *p_usage, bool p_is_cast = false);
	void update_array_literal_element_type(GDScriptParser::ArrayNode *p_array, const GDScriptParser::DataType &p_element_type);
	void update_dictionary_literal_element_type(GDScriptParser::DictionaryNode *p_dictionary, const GDScriptParser::DataType &p_key_element_type, const GDScriptParser::DataType &p_value_element_type);
	bool is_type_compatible(const GDScriptParser::DataType &p_target, const GDScriptParser::DataType &p_source, bool p_allow_implicit_conversion = false, const GDScriptParser::Node *p_source_node = nullptr);
	void push_error(const String &p_message, const GDScriptParser::Node *p_origin = nullptr);
	void mark_node_unsafe(const GDScriptParser::Node *p_node);
	void downgrade_node_type_source(GDScriptParser::Node *p_node);
	void mark_lambda_use_self();
	void resolve_pending_lambda_bodies();
	void reduce_identifier_from_base_set_class(GDScriptParser::IdentifierNode *p_identifier, GDScriptParser::DataType p_identifier_datatype);
	Ref<GDScriptParserRef> ensure_cached_external_parser_for_class(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_from_class, const char *p_context, const GDScriptParser::Node *p_source);
	Ref<GDScriptParserRef> find_cached_external_parser_for_class(const GDScriptParser::ClassNode *p_class, const Ref<GDScriptParserRef> &p_dependant_parser);
	Ref<GDScriptParserRef> find_cached_external_parser_for_class(const GDScriptParser::ClassNode *p_class, GDScriptParser *p_dependant_parser);
	Ref<GDScript> get_depended_shallow_script(const String &p_path, Error &r_error);
#ifdef DEBUG_ENABLED
	void is_shadowing(GDScriptParser::IdentifierNode *p_identifier, const String &p_context, const bool p_in_local_scope);
#endif

public:
	Error resolve_inheritance();
	Error resolve_interface();
	Error resolve_body();
	Error resolve_dependencies();
	Error analyze();
	void set_strict_null_checks(bool p_enabled) { strict_null_checks = p_enabled; }
	void set_strict_dynamic_checks(bool p_enabled) { strict_dynamic_checks = p_enabled; }

	Variant make_variable_default_value(GDScriptParser::VariableNode *p_variable);

	static bool check_type_compatibility(const GDScriptParser::DataType &p_target, const GDScriptParser::DataType &p_source, bool p_allow_implicit_conversion = false, const GDScriptParser::Node *p_source_node = nullptr);
	static GDScriptParser::DataType type_from_metatype(const GDScriptParser::DataType &p_meta_type);
	static bool class_exists(const StringName &p_class);

	GDScriptAnalyzer(GDScriptParser *p_parser);
};
