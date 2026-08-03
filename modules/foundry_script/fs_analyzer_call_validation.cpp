/**************************************************************************/
/*  fs_analyzer_call_validation.cpp                                       */
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
#include "fs_type.h"

#include "core/object/class_db.h"

static FSParser::DataType make_void_type() {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::BUILTIN;
	type.builtin_type = Variant::NIL;
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

static String callable_type_string_with_signature(
		const FSParser::DataType &p_callable_type,
		const Vector<FSParser::DataType> &p_callable_parameter_types) {
	if (p_callable_type.has_explicit_method_signature || p_callable_parameter_types.is_empty()) {
		return p_callable_type.to_string();
	}

	FSParser::DataType callable_type = p_callable_type;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.method_parameter_types = p_callable_parameter_types;
	callable_type.method_return_type.push_back(make_void_type());
	return callable_type.to_string();
}

// A locally declared signal carries its per-parameter signature without the explicit-annotation flag,
// so `to_string()` would render it as a bare "Signal". Rendering it from the parameters it does carry
// keeps the diagnostics for `mysignal.connect(handler)` identical to `connect("mysignal", handler)`.
static String signal_type_string_with_signature(const FSParser::DataType &p_signal_type) {
	if (p_signal_type.has_explicit_method_signature || p_signal_type.method_parameter_types.is_empty()) {
		return p_signal_type.to_string();
	}

	FSParser::DataType signal_type = p_signal_type;
	signal_type.has_explicit_method_signature = true;
	return signal_type.to_string();
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

static void collect_method_type_parameter_bounds(const FSParser::DataType &p_type, HashMap<StringName, FSParser::DataType> &r_bounds) {
	if (p_type.kind == FSParser::DataType::TYPE_PARAMETER &&
			p_type.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_METHOD &&
			!p_type.type_parameter_bound.is_empty() && !r_bounds.has(p_type.type_parameter_name)) {
		r_bounds.insert(p_type.type_parameter_name, p_type.type_parameter_bound[0]);
	}
	for (const FSParser::DataType &element : p_type.container_element_types) {
		collect_method_type_parameter_bounds(element, r_bounds);
	}
	for (const FSParser::DataType &argument : p_type.type_arguments) {
		collect_method_type_parameter_bounds(argument, r_bounds);
	}
}

FSAnalyzer::CallSiteValidationContext::CallSiteValidationContext(FSAnalyzer *p_analyzer) :
		analyzer(p_analyzer) {
}

static bool _method_signature_accepts_argument_count(int p_argument_count, int p_parameter_count, int p_default_args_count, bool p_is_vararg, const Vector<int> &p_extra_allowed_argument_counts) {
	const int min_argument_count = p_parameter_count - p_default_args_count;
	if (p_argument_count >= min_argument_count && (p_is_vararg || p_argument_count <= p_parameter_count)) {
		return true;
	}

	for (int extra_argument_count : p_extra_allowed_argument_counts) {
		if (extra_argument_count == p_argument_count) {
			return true;
		}
	}

	return false;
}

bool FSAnalyzer::CallSiteValidationContext::merge_inferred_type_argument(const FSParser::DataType &p_existing, const FSParser::DataType &p_candidate, FSParser::DataType &r_merged) {
	// Type parameters are invariant (epic #125 design): a parameter solved from several arguments
	// must resolve to the same type each time. Differing types conflict and require explicit
	// application.
	const bool existing_is_parameter = p_existing.kind == FSParser::DataType::TYPE_PARAMETER;
	const bool candidate_is_parameter = p_candidate.kind == FSParser::DataType::TYPE_PARAMETER;
	if (existing_is_parameter || candidate_is_parameter) {
		// A type-parameter argument (e.g. an outer `U` forwarded into this call) unifies only with
		// the identical parameter. `analyzer->is_type_compatible()` treats an erased parameter as Variant-like
		// and would otherwise merge `U` with an unrelated concrete type without flagging a conflict.
		if (existing_is_parameter && candidate_is_parameter &&
				p_existing.type_parameter_name == p_candidate.type_parameter_name &&
				p_existing.type_parameter_scope == p_candidate.type_parameter_scope) {
			r_merged = p_existing;
			return true;
		}
		return false;
	}

	// Concrete types: mutual assignability without implicit conversion is the invariant equality
	// test, tolerating incidental DataType field differences between two arguments of the same type.
	if (p_existing.kind == p_candidate.kind &&
			analyzer->is_type_compatible(p_existing, p_candidate) && analyzer->is_type_compatible(p_candidate, p_existing)) {
		r_merged = p_existing;
		return true;
	}
	return false;
}

void FSAnalyzer::CallSiteValidationContext::collect_type_parameter_bindings(const FSParser::DataType &p_parameter_type, const FSParser::DataType &p_argument_type,
		HashMap<StringName, FSParser::DataType> &r_bindings, HashSet<StringName> &r_conflicts) {
	if (p_parameter_type.kind == FSParser::DataType::TYPE_PARAMETER &&
			p_parameter_type.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_METHOD) {
		// Only a usable, concrete argument type constrains a parameter; a Variant or untyped
		// argument leaves it open for another argument (or explicit application) to solve.
		if (!p_argument_type.is_set() || p_argument_type.is_variant() || !p_argument_type.is_hard_type()) {
			return;
		}

		FSParser::DataType candidate = p_argument_type;
		if (p_parameter_type.is_type_handle_annotation) {
			if (!candidate.is_meta_type && !candidate.is_type_handle_annotation) {
				return;
			}
			candidate = type_handle_represented_type(candidate);
		} else {
			candidate.is_meta_type = false;
		}
		candidate.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;

		const StringName &name = p_parameter_type.type_parameter_name;
		if (r_conflicts.has(name)) {
			return;
		}
		FSParser::DataType *existing = r_bindings.getptr(name);
		if (existing == nullptr) {
			r_bindings.insert(name, candidate);
			return;
		}
		FSParser::DataType merged;
		if (merge_inferred_type_argument(*existing, candidate, merged)) {
			*existing = merged;
		} else {
			r_conflicts.insert(name);
		}
		return;
	}

	// Unify structurally through matching containers (`Array[T]`, `Dictionary[K, V]`) and
	// specialized handles (`Box[T]`), so a parameter nested inside a type argument is solved too.
	const int parameter_element_count = p_parameter_type.container_element_types.size();
	if (parameter_element_count > 0 && parameter_element_count == p_argument_type.container_element_types.size()) {
		for (int i = 0; i < parameter_element_count; i++) {
			collect_type_parameter_bindings(p_parameter_type.container_element_types[i], p_argument_type.container_element_types[i], r_bindings, r_conflicts);
		}
	}

	const int parameter_argument_count = p_parameter_type.type_arguments.size();
	if (parameter_argument_count > 0 && parameter_argument_count == p_argument_type.type_arguments.size()) {
		for (int i = 0; i < parameter_argument_count; i++) {
			collect_type_parameter_bindings(p_parameter_type.type_arguments[i], p_argument_type.type_arguments[i], r_bindings, r_conflicts);
		}
	}
}

void FSAnalyzer::CallSiteValidationContext::apply_generic_method_call(FSParser::CallNode *p_call, FSParser::FunctionNode *p_function,
		List<FSParser::DataType> &r_par_types, FSParser::DataType &r_return_type) {
	const Vector<FSParser::TypeParameterNode *> &type_parameters = p_function->type_parameters;
	if (type_parameters.is_empty()) {
		return;
	}

	// A parameter that cannot be solved falls back to Variant so the rest of the call stays
	// type-checkable after the inference error is reported, without cascading "cannot infer" noise.
	FSParser::DataType unresolved_fallback;
	unresolved_fallback.kind = FSParser::DataType::VARIANT;
	unresolved_fallback.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;

	HashMap<StringName, FSParser::DataType> bindings;
	// Parameters that could not be solved (conflicting or unconstrained); their fallback Variant
	// binding must not be re-reported as a bound violation.
	HashSet<StringName> failed_parameters;

	// Explicit type arguments (`swap[int](...)`) short-circuit inference.
	bool explicit_application = false;
	if (p_call->callee != nullptr && p_call->callee->type == FSParser::Node::SUBSCRIPT) {
		FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(p_call->callee);
		if (!subscript->is_attribute && subscript->index != nullptr) {
			explicit_application = true;

			Vector<FSParser::ExpressionNode *> argument_expressions;
			if (subscript->type_arguments.is_empty()) {
				argument_expressions.push_back(subscript->index);
			} else {
				argument_expressions = subscript->type_arguments;
			}

			// Resolve positionally: a failed argument keeps its slot (filled with the Variant
			// fallback and flagged) so a later argument is never shifted into an earlier type
			// parameter, and the arity check sees the count the user actually wrote.
			Vector<FSParser::DataType> explicit_arguments;
			Vector<bool> explicit_argument_failed;
			for (int argument_index = 0; argument_index < argument_expressions.size(); argument_index++) {
				FSParser::ExpressionNode *argument_expression = argument_expressions[argument_index];
				FSParser::DataType type_argument;
				if (analyzer->resolve_explicit_type_argument(argument_expression, type_argument)) {
					if (argument_index < subscript->type_argument_is_nullable.size()) {
						analyzer->apply_use_site_nullable_type_argument_marker(type_argument, subscript->type_argument_is_nullable[argument_index]);
					}
					explicit_arguments.push_back(type_argument);
					explicit_argument_failed.push_back(false);
				} else {
					analyzer->push_error(vformat(R"*(Could not resolve the explicit type argument for generic method "%s()".)*", p_function->identifier->name), argument_expression);
					explicit_arguments.push_back(unresolved_fallback);
					explicit_argument_failed.push_back(true);
				}
			}

			if (explicit_arguments.size() != type_parameters.size()) {
				analyzer->push_error(vformat(R"*(Generic method "%s()" expects %d type argument(s), but %d %s given.)*", p_function->identifier->name, type_parameters.size(), explicit_arguments.size(), explicit_arguments.size() == 1 ? "was" : "were"), subscript);
				explicit_arguments.clear();
				explicit_argument_failed.clear();
			}

			const int binding_count = MIN(explicit_arguments.size(), type_parameters.size());
			for (int i = 0; i < binding_count; i++) {
				const FSParser::TypeParameterNode *parameter = type_parameters[i];
				if (parameter == nullptr || parameter->identifier == nullptr) {
					continue;
				}
				bindings.insert(parameter->identifier->name, explicit_arguments[i]);
				if (explicit_argument_failed[i]) {
					// Resolution already failed and was reported; keep the slot bound to Variant and
					// skip its bound check rather than re-diagnosing.
					failed_parameters.insert(parameter->identifier->name);
				}
			}
		}
	}

	// Solve any still-open parameters by unifying each argument against its declared type.
	if (!explicit_application) {
		HashSet<StringName> conflicts;
		int parameter_index = 0;
		for (const FSParser::DataType &declared_parameter_type : r_par_types) {
			if (parameter_index >= p_call->arguments.size()) {
				break;
			}
			// A default the analyzer synthesized for a skipped middle parameter must not constrain a
			// type parameter, mirroring a trailing omitted default that never participates here.
			if (p_call->synthesized_argument_indices.has(parameter_index)) {
				parameter_index++;
				continue;
			}
			const FSParser::ExpressionNode *argument = p_call->arguments[parameter_index];
			if (argument != nullptr) {
				collect_type_parameter_bindings(declared_parameter_type, argument->get_datatype(), bindings, conflicts);
			}
			parameter_index++;
		}

		for (const StringName &conflicted : conflicts) {
			analyzer->push_error(vformat(R"*(Could not infer type parameter "%s" of generic method "%s()" because its arguments have conflicting types. Apply the type arguments explicitly, e.g. "%s[...](...)".)*", conflicted, p_function->identifier->name, p_function->identifier->name), p_call);
			// Keep the call type-checkable: an unresolved parameter falls back to Variant.
			bindings.insert(conflicted, unresolved_fallback);
			failed_parameters.insert(conflicted);
		}
	}

	// Every parameter must end up bound. Under inference, a still-open parameter is one no
	// argument constrained, which is an error directing the user to explicit application. Under
	// explicit application a gap means resolution already failed and was reported above, so it
	// just falls back without a second diagnostic.
	for (const FSParser::TypeParameterNode *parameter : type_parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		if (!bindings.has(parameter->identifier->name)) {
			if (!explicit_application) {
				analyzer->push_error(vformat(R"*(Could not infer type parameter "%s" of generic method "%s()" from its arguments. Apply the type arguments explicitly, e.g. "%s[...](...)".)*", parameter->identifier->name, p_function->identifier->name, p_function->identifier->name), p_call);
			}
			bindings.insert(parameter->identifier->name, unresolved_fallback);
			failed_parameters.insert(parameter->identifier->name);
		}
	}

	if (bindings.is_empty()) {
		return;
	}

	// A solved type argument (inferred or explicit) must satisfy its parameter's upper bound. A
	// bound's resolved datatype is cached on its declaration once the parameter is used anywhere
	// (signature or body); the signature traversal is a fallback for bounds reached only there.
	HashMap<StringName, FSParser::DataType> parameter_bounds;
	for (const FSParser::DataType &parameter_type : r_par_types) {
		collect_method_type_parameter_bounds(parameter_type, parameter_bounds);
	}
	collect_method_type_parameter_bounds(r_return_type, parameter_bounds);
	for (const FSParser::TypeParameterNode *parameter : type_parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr || parameter->bound == nullptr) {
			continue;
		}
		// The bound collected from the (already receiver-specialized) signature wins; the raw
		// declaration is only a fallback for a parameter whose bound is reached nowhere in the
		// signature, so it must not overwrite the specialized one (e.g. `[U: T]` with `T := PackedScene`).
		if (parameter_bounds.has(parameter->identifier->name)) {
			continue;
		}
		const FSParser::DataType bound_type = parameter->bound->get_datatype();
		if (bound_type.is_set() && bound_type.kind != FSParser::DataType::UNRESOLVED && bound_type.kind != FSParser::DataType::RESOLVING) {
			parameter_bounds[parameter->identifier->name] = FSAnalyzer::type_from_metatype(bound_type);
		}
	}

	for (const FSParser::TypeParameterNode *parameter : type_parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		const StringName &name = parameter->identifier->name;
		if (failed_parameters.has(name)) {
			continue;
		}
		const FSParser::DataType *binding = bindings.getptr(name);
		const FSParser::DataType *bound = parameter_bounds.getptr(name);
		if (binding == nullptr || bound == nullptr || bound->kind == FSParser::DataType::UNRESOLVED) {
			continue;
		}
		// A dependent bound (`[U: Resource, T: U]`) is resolved against the sibling's solved type,
		// so substitute the collected bindings into the bound before checking.
		const FSParser::DataType effective_bound = FSParser::DataType::substitute(*bound, bindings);
		if (!analyzer->type_argument_satisfies_bound(*binding, effective_bound)) {
			analyzer->push_error(vformat(R"*(Type argument "%s" does not satisfy the bound "%s" of type parameter "%s" of generic method "%s()".)*", binding->to_string(), effective_bound.to_string(), name, p_function->identifier->name), p_call);
		}
	}

	for (FSParser::DataType &parameter_type : r_par_types) {
		parameter_type = FSParser::DataType::substitute(parameter_type, bindings);
	}

	// A typed-container return whose element involves a method type parameter (`-> Array[T]`) is erased
	// at runtime: the method, compiled once, returns an untyped container. We still substitute the
	// static type to the concrete container below, so flag the call here (before erasing the marker) so
	// an assignment to a concrete typed container retypes the untyped runtime value.
	if (p_call != nullptr) {
		for (int i = 0; i < r_return_type.container_element_types.size(); i++) {
			if (analyzer->signature_type_involves_type_parameter(r_return_type.container_element_types[i])) {
				p_call->returns_erased_container = true;
				break;
			}
		}
	}

	r_return_type = FSParser::DataType::substitute(r_return_type, bindings);
}

bool FSAnalyzer::CallSiteValidationContext::callable_signature_from_type(const FSParser::DataType &p_callable_type, Vector<FSParser::DataType> &r_par_types, int &r_default_arg_count, bool &r_is_vararg) const {
	if (p_callable_type.kind != FSParser::DataType::BUILTIN || p_callable_type.builtin_type != Variant::CALLABLE || !p_callable_type.has_method_signature) {
		return false;
	}

	r_par_types.clear();
	r_default_arg_count = 0;
	r_is_vararg = false;

	if (p_callable_type.has_explicit_method_signature) {
		r_par_types = p_callable_type.method_parameter_types;
		r_default_arg_count = p_callable_type.method_info.default_arguments.size();
		r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
		return true;
	}

	if (p_callable_type.method_parameter_types.size() == p_callable_type.method_info.arguments.size()) {
		r_par_types = p_callable_type.method_parameter_types;
		r_default_arg_count = p_callable_type.method_info.default_arguments.size();
		r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
		return true;
	}

	for (const PropertyInfo &E : p_callable_type.method_info.arguments) {
		r_par_types.push_back(analyzer->type_from_property(E, true));
	}
	r_default_arg_count = p_callable_type.method_info.default_arguments.size();
	r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
	return true;
}

FSParser::DataType FSAnalyzer::CallSiteValidationContext::plain_callable_type() const {
	return analyzer->type_from_property(PropertyInfo(Variant::CALLABLE, ""));
}

FSParser::DataType FSAnalyzer::CallSiteValidationContext::over_bound_callable_type(const FSParser::DataType &p_source_callable_type) const {
	// Binding more arguments than a fixed-arity target accepts produces a callable that cannot be
	// invoked successfully. There is no precise signature to describe it, so return a signatureless
	// callable that still carries the source's async marker (bind()/unbind() preserve async-ness).
	FSParser::DataType callable_type = plain_callable_type();
	callable_type.signature_is_async = p_source_callable_type.signature_is_async;
	callable_type.callable_is_over_bound = true;
	return callable_type;
}

FSParser::DataType FSAnalyzer::CallSiteValidationContext::explicit_callable_type_from_signature(const FSParser::DataType &p_return_type, const Vector<FSParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg, bool p_is_async) const {
	FSParser::DataType callable_type = plain_callable_type();
	callable_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.signature_is_async = p_is_async;
	callable_type.method_parameter_types = p_parameter_types;
	callable_type.method_return_type.push_back(p_return_type);
	callable_type.method_info.return_val = p_return_type.to_property_info("");
	for (int i = 0; i < p_parameter_types.size(); i++) {
		callable_type.method_info.arguments.push_back(p_parameter_types[i].to_property_info("arg" + itos(i + 1)));
	}
	callable_type.method_info.default_arguments.resize(p_default_arg_count);
	if (p_is_vararg) {
		callable_type.method_info.flags |= METHOD_FLAG_VARARG;
	}
	return callable_type;
}

FSParser::DataType FSAnalyzer::CallSiteValidationContext::transformed_callable_type(const FSParser::DataType &p_source_callable_type, const Vector<FSParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const {
	FSParser::DataType callable_type = p_source_callable_type;
	callable_type.kind = FSParser::DataType::BUILTIN;
	callable_type.builtin_type = Variant::CALLABLE;
	callable_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.method_parameter_types = p_parameter_types;
	callable_type.method_extra_allowed_argument_counts.clear();
	callable_type.method_unbound_argument_count = 0;
	callable_type.method_info.arguments.clear();
	for (int i = 0; i < p_parameter_types.size(); i++) {
		callable_type.method_info.arguments.push_back(p_parameter_types[i].to_property_info("arg" + itos(i + 1)));
	}
	callable_type.method_info.default_arguments.clear();
	callable_type.method_info.default_arguments.resize(p_default_arg_count);
	if (p_is_vararg) {
		callable_type.method_info.flags |= METHOD_FLAG_VARARG;
	} else {
		callable_type.method_info.flags &= ~uint32_t(METHOD_FLAG_VARARG);
	}
	if (!callable_type.method_return_type.is_empty()) {
		callable_type.method_info.return_val = callable_type.method_return_type[0].to_property_info("");
	}
	return callable_type;
}

FSParser::DataType FSAnalyzer::CallSiteValidationContext::explicit_callable_type_from_info(const MethodInfo &p_info) const {
	FSParser::DataType callable_type = make_callable_type(p_info);
	callable_type.method_parameter_types.clear();
	for (const PropertyInfo &argument : p_info.arguments) {
		callable_type.method_parameter_types.push_back(analyzer->type_from_property(argument, true));
	}
	callable_type.method_return_type.clear();
	callable_type.method_return_type.push_back(analyzer->type_from_property(p_info.return_val));
	callable_type.has_explicit_method_signature = true;
	return callable_type;
}

FSParser::ArrayNode *FSAnalyzer::CallSiteValidationContext::array_literal_argument(const FSParser::CallNode *p_call, int p_argument_index) const {
	if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return nullptr;
	}

	FSParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr || argument->type != FSParser::Node::ARRAY) {
		return nullptr;
	}

	return static_cast<FSParser::ArrayNode *>(argument);
}

bool FSAnalyzer::CallSiteValidationContext::callable_type_from_method(const FSParser::DataType &p_receiver_type, const StringName &p_method_name, FSParser::Node *p_source, FSParser::DataType &r_callable_type) {
	FSParser::DataType return_type;
	List<FSParser::DataType> parameter_types;
	int default_arg_count = 0;
	BitField<MethodFlags> method_flags = {};
	if (!analyzer->get_function_signature(p_source, false, p_receiver_type, p_method_name, return_type, parameter_types, default_arg_count, method_flags)) {
		return false;
	}

	Vector<FSParser::DataType> parameter_type_vector;
	for (const FSParser::DataType &parameter_type : parameter_types) {
		parameter_type_vector.push_back(parameter_type);
	}
	r_callable_type = explicit_callable_type_from_signature(return_type, parameter_type_vector, default_arg_count, method_flags.has_flag(METHOD_FLAG_VARARG), method_flags.has_flag(METHOD_FLAG_ASYNC));
	return true;
}

bool FSAnalyzer::CallSiteValidationContext::callable_type_from_constant_method_args(const FSParser::CallNode *p_call, int p_receiver_arg_index, int p_method_arg_index, FSParser::DataType &r_callable_type) {
	if (p_receiver_arg_index < 0 || p_receiver_arg_index >= p_call->arguments.size()) {
		return false;
	}

	StringName method_name;
	if (!analyzer->string_name_from_constant_arg(p_call, p_method_arg_index, method_name)) {
		return false;
	}

	return callable_type_from_method(p_call->arguments[p_receiver_arg_index]->get_datatype(), method_name, const_cast<FSParser::CallNode *>(p_call), r_callable_type);
}

bool FSAnalyzer::CallSiteValidationContext::call_argument_can_be_string_name(const FSParser::CallNode *p_call, int p_argument_index) {
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

	const FSParser::DataType string_name_type = analyzer->type_from_property(PropertyInfo(Variant::STRING_NAME, ""), true);
	const FSParser::DataType string_type = analyzer->type_from_property(PropertyInfo(Variant::STRING, ""), true);
	return analyzer->is_type_compatible(string_name_type, argument_type, true) || analyzer->is_type_compatible(string_type, argument_type, true);
}

void FSAnalyzer::CallSiteValidationContext::validate_strict_callable_method_fallback(const FSParser::CallNode *p_call, const FSParser::DataType &p_receiver_type, int p_method_arg_index) {
	if (!analyzer->strict_dynamic_checks || p_call == nullptr || p_method_arg_index < 0 || p_method_arg_index >= p_call->arguments.size()) {
		return;
	}

	StringName method_name;
	if (analyzer->string_name_from_constant_arg(p_call, p_method_arg_index, method_name)) {
		analyzer->push_error(vformat(R"*(Cannot resolve method "%s" on type "%s" for Callable construction in strict dynamic mode.)*", method_name, p_receiver_type.to_string()), p_call->arguments[p_method_arg_index]);
	} else if (call_argument_can_be_string_name(p_call, p_method_arg_index)) {
		analyzer->push_error("Cannot use dynamic method name for Callable construction in strict dynamic mode.", p_call->arguments[p_method_arg_index]);
	}
}

bool FSAnalyzer::CallSiteValidationContext::call_has_named_arguments(const FSParser::CallNode *p_call) {
	for (int i = 0; i < p_call->argument_names.size(); i++) {
		if (p_call->argument_names[i] != StringName()) {
			return true;
		}
	}
	return false;
}

void FSAnalyzer::CallSiteValidationContext::reject_named_call_arguments(const FSParser::CallNode *p_call) {
	// Named arguments are resolved entirely at compile time against a statically known
	// FoundryScript signature. For any other callee (builtin constructors, engine utility
	// functions, native methods, or dynamic/`Callable` targets) the parameter names are
	// unavailable, so reject them instead of silently dropping the names.
	for (int i = 0; i < p_call->argument_names.size(); i++) {
		if (p_call->argument_names[i] != StringName()) {
			analyzer->push_error("Named arguments require a statically known FoundryScript function.", p_call->arguments[i]);
			return;
		}
	}
}

bool FSAnalyzer::CallSiteValidationContext::canonicalize_named_call_arguments(FSParser::CallNode *p_call, const FSParser::FunctionNode *p_function) {
	// The parser keeps `argument_names` parallel to `arguments`, with an empty name for each
	// positional argument. Map every `name = value` argument to its parameter position, rewrite
	// the call into canonical positional order, and clear the names so the rest of the analyzer,
	// codegen, and the VM see an ordinary positional call.
	if (p_call->argument_names.size() != p_call->arguments.size() || !call_has_named_arguments(p_call)) {
		// Nothing to canonicalize; drop any (all-empty) name metadata for a clean positional call.
		p_call->argument_names.clear();
		return true;
	}

	const int parameter_count = p_function->parameters.size();
	const StringName function_name = p_function->identifier != nullptr ? p_function->identifier->name : StringName();

	// Rule: once a named argument appears, every following argument must be named.
	int positional_count = 0;
	bool seen_named = false;
	for (int i = 0; i < p_call->arguments.size(); i++) {
		const bool is_named = p_call->argument_names[i] != StringName();
		if (is_named) {
			seen_named = true;
		} else if (seen_named) {
			analyzer->push_error("Positional argument cannot follow a named argument.", p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		} else {
			positional_count++;
		}
	}

	Vector<FSParser::ExpressionNode *> slots;
	slots.resize(parameter_count);
	for (int i = 0; i < parameter_count; i++) {
		slots.write[i] = nullptr;
	}

	// Track the source (written) position of the argument occupying each slot so the canonical call
	// can carry a source-order evaluation list for codegen. A slot left at -1 holds either nothing or
	// a synthesized constant gap-fill, neither of which has an observable evaluation position.
	Vector<int> slot_source_index;
	slot_source_index.resize(parameter_count);
	for (int i = 0; i < parameter_count; i++) {
		slot_source_index.write[i] = -1;
	}

	// Positional arguments fill the leading parameter slots in order. Arguments beyond the fixed
	// parameter count belong to a rest parameter and keep their order after the fixed slots.
	Vector<FSParser::ExpressionNode *> rest_arguments;
	Vector<int> rest_source_index;
	for (int i = 0; i < positional_count; i++) {
		if (i < parameter_count) {
			slots.write[i] = p_call->arguments[i];
			slot_source_index.write[i] = i;
		} else {
			rest_arguments.push_back(p_call->arguments[i]);
			rest_source_index.push_back(i);
		}
	}

	for (int i = positional_count; i < p_call->arguments.size(); i++) {
		const StringName &argument_name = p_call->argument_names[i];
		if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr && p_function->rest_parameter->identifier->name == argument_name) {
			analyzer->push_error(vformat(R"(The rest parameter "%s" cannot be passed by name.)", argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		const int *parameter_index = p_function->parameters_indices.getptr(argument_name);
		if (parameter_index == nullptr) {
			analyzer->push_error(vformat(R"*(Function "%s()" has no parameter named "%s".)*", function_name, argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		if (slots[*parameter_index] != nullptr) {
			analyzer->push_error(vformat(R"(Parameter "%s" was specified more than once.)", argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		slots.write[*parameter_index] = p_call->arguments[i];
		slot_source_index.write[*parameter_index] = i;
	}

	int max_filled_index = -1;
	for (int i = 0; i < parameter_count; i++) {
		if (slots[i] != nullptr) {
			max_filled_index = i;
		}
	}

	// Every parameter slot a named call leaves empty must be resolved here. A required parameter with
	// no value is a compile error named after the parameter. An omitted parameter before the last
	// filled slot has no positional argument, so its constant default must be inlined at the call site
	// to keep the canonical order correct; FoundryScript defaults run in the callee's scope and may
	// reference `self`, members, or earlier parameters, so only a compile-time-constant default is
	// safe to materialize here, and anything else is a compile error. A trailing omitted parameter is
	// left out so the callee applies its own default at runtime, exactly as a positional call that
	// omits trailing arguments would.
	//
	// The inlined value is the statically resolved callee's default, matching the rest of the
	// feature's compile-time model (the call is also type-checked against that static signature), as
	// C# resolves optional-argument defaults from the compile-time receiver type. If a subclass
	// overrides the method with a different default, a base-typed receiver dispatched to that override
	// still receives the static default rather than the override's. This is intended: a non-trailing
	// gap can only be filled without an ABI change by inlining at the call site, while a trailing
	// omission defers to the callee's runtime default mechanism, so the two paths necessarily diverge
	// when an override changes a constant default. Honoring the runtime override here would require the
	// presence-bitmask calling convention the feature explicitly rules out as a non-goal.
	for (int i = 0; i < parameter_count; i++) {
		if (slots[i] != nullptr) {
			continue;
		}

		const FSParser::ParameterNode *parameter = p_function->parameters[i];
		const StringName parameter_name = parameter->identifier != nullptr ? parameter->identifier->name : StringName();

		if (parameter->initializer == nullptr) {
			// A required parameter received no argument. Mirror the positional too-few-arguments error
			// but name the specific parameter the named call left unfilled.
			analyzer->push_error(vformat(R"(Missing value for required parameter "%s".)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}

		if (i >= max_filled_index) {
			// Trailing optional parameter: leave it out so the callee supplies its own default.
			continue;
		}

		// Interior gap with a default. The default must be materialized as a constant at the call
		// site. A parameter whose type depends on a type parameter is still inlined here, but the
		// synthesized argument is recorded so generic inference and post-substitution validation skip
		// it: its type is substituted from the receiver's (or method's) type arguments, and a baked
		// default must behave like a trailing omitted default, which never participates in either.
		if (!parameter->initializer->is_constant) {
			analyzer->push_error(vformat(R"(Cannot skip parameter "%s": its default value is not a constant expression. Pass it explicitly.)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}

		// Synthesize a constant argument from the parameter's default. Marking it constant routes it
		// through the normal constant-argument path in `validate_call_arg`, which applies the same
		// builtin-type conversion a written literal would receive. A class-metatype default (e.g.
		// `cls = SomeClass`) carries the analyzer's reduced class object, which may be a shallow
		// same-unit class; the compiler re-points it to the live compiled subclass when it lowers the
		// synthesized literal, exactly as it does for class-constant identifiers and `const` aliases.
		FSParser::LiteralNode *constant_argument = analyzer->parser->alloc_node<FSParser::LiteralNode>();
		constant_argument->value = parameter->initializer->reduced_value;
		constant_argument->reduced = true;
		constant_argument->is_constant = true;
		constant_argument->reduced_value = parameter->initializer->reduced_value;
		constant_argument->set_datatype(parameter->initializer->get_datatype());
		slots.write[i] = constant_argument;
		// Canonical order has no gaps below `max_filled_index`, so the canonical position of this
		// synthesized argument equals its parameter index.
		p_call->synthesized_argument_indices.insert(i);
	}

	Vector<FSParser::ExpressionNode *> canonical_arguments;
	// Pair each canonical argument with the source position that determines its evaluation order.
	// Real arguments use their written position; synthesized constant gap-fills carry no source
	// position and are evaluated last (they have no side effects, so their order is irrelevant).
	Vector<int> canonical_source_index;
	for (int i = 0; i <= max_filled_index; i++) {
		canonical_arguments.push_back(slots[i]);
		canonical_source_index.push_back(slot_source_index[i]);
	}
	for (int i = 0; i < rest_arguments.size(); i++) {
		canonical_arguments.push_back(rest_arguments[i]);
		canonical_source_index.push_back(rest_source_index[i]);
	}

	// Build the source-order evaluation list: canonical indices sorted by written position, with the
	// side-effect-free synthesized constants appended in canonical order. The compiler evaluates the
	// argument expressions in this order while still binding them to parameters positionally, so a
	// named call's side effects run left to right as written. Only record it when it differs from the
	// canonical order; an identity permutation lets the compiler keep its default front-to-back walk.
	Vector<int> evaluation_order;
	bool reordered = false;
	for (int source = 0; source < p_call->arguments.size(); source++) {
		for (int canonical = 0; canonical < canonical_source_index.size(); canonical++) {
			if (canonical_source_index[canonical] == source) {
				if (canonical != evaluation_order.size()) {
					reordered = true;
				}
				evaluation_order.push_back(canonical);
				break;
			}
		}
	}
	for (int canonical = 0; canonical < canonical_source_index.size(); canonical++) {
		if (canonical_source_index[canonical] == -1) {
			if (canonical != evaluation_order.size()) {
				reordered = true;
			}
			evaluation_order.push_back(canonical);
		}
	}

	p_call->arguments = canonical_arguments;
	p_call->argument_names.clear();
	if (reordered) {
		p_call->argument_evaluation_order = evaluation_order;
	}
	return true;
}

void FSAnalyzer::CallSiteValidationContext::validate_call_arg(const MethodInfo &p_method, const FSParser::CallNode *p_call) {
	List<FSParser::DataType> arg_types;

	for (const PropertyInfo &E : p_method.arguments) {
		arg_types.push_back(analyzer->type_from_property(E, true));
	}

	// Cache the resolved parameter types for editor refactors (e.g. insert-explicit-cast),
	// matching the user-function call path. The analyzer owns the parsed tree, so writing
	// through the const handle is sound.
	FSParser::CallNode *mutable_call = const_cast<FSParser::CallNode *>(p_call);
	mutable_call->resolved_parameter_types.clear();
	for (const FSParser::DataType &arg_type : arg_types) {
		mutable_call->resolved_parameter_types.push_back(arg_type);
	}

	validate_call_arg(arg_types, p_method.default_arguments.size(), (p_method.flags & METHOD_FLAG_VARARG) != 0, p_call);
}

String FSAnalyzer::CallSiteValidationContext::make_invalid_argument_error(
		const StringName &p_function,
		int p_argument_number,
		const FSParser::DataType &p_expected_type,
		const FSParser::DataType &p_actual_type,
		bool p_strict_dynamic_mismatch,
		bool p_strict_nullable_mismatch,
		const FSParser::Node *p_actual_node) const {
	if (p_strict_dynamic_mismatch) {
		return vformat(R"*(Cannot pass Variant value as argument %d of "%s()" in strict dynamic mode; expected "%s".)*",
				p_argument_number,
				p_function,
				p_expected_type.to_string());
	}
	if (p_strict_nullable_mismatch) {
		return vformat(R"*(Cannot pass nullable value of type "%s" as argument %d of "%s()"; expected non-nullable "%s".)*",
				p_actual_type.to_string(),
				p_argument_number,
				p_function,
				p_expected_type.to_string());
	}
	const String type_handle_error = analyzer->make_type_handle_argument_error(
			p_function,
			p_argument_number,
			p_expected_type,
			p_actual_type,
			p_actual_node);
	if (!type_handle_error.is_empty()) {
		return type_handle_error;
	}
	return vformat(R"*(Invalid argument for "%s()" function: argument %d should be "%s" but is "%s".)*",
			p_function,
			p_argument_number,
			p_expected_type.to_string(),
			p_actual_type.to_string());
}

const FSParser::DataType *FSAnalyzer::CallSiteValidationContext::rest_element_type(const FSParser::DataType *p_rest_parameter_type) {
	if (p_rest_parameter_type == nullptr || !FSAnalyzer::rest_parameter_type_is_narrowing(*p_rest_parameter_type)) {
		return nullptr;
	}
	return &p_rest_parameter_type->container_element_types[0];
}

void FSAnalyzer::CallSiteValidationContext::validate_argument_against_type(const FSParser::DataType &p_expected_type, FSParser::ExpressionNode *p_argument, int p_argument_number, const StringName &p_function, const FSParser::CallNode *p_call) {
	FSParser::DataType par_type = p_expected_type;

	analyzer->mark_coroutine_handle_capture(p_argument, par_type);

	if (par_type.is_hard_type() && p_argument->is_constant) {
		analyzer->update_const_expression_builtin_type(p_argument, par_type, "pass");
	}
	FSParser::DataType arg_type = p_argument->get_datatype();

	if (analyzer->datatype_contains_self_type_parameter(par_type)) {
		if (!analyzer->datatype_matches_self_parameter_contract(par_type, arg_type) &&
				!(p_call != nullptr && analyzer->is_bare_self_value_parameter(par_type) &&
						analyzer->call_argument_is_same_receiver(p_call, p_argument))) {
			analyzer->push_error(make_invalid_argument_error(p_function, p_argument_number, par_type, arg_type, false, false, p_argument), p_argument);
		}
		return;
	}

	if (arg_type.is_variant() || !arg_type.is_hard_type()) {
		if (arg_type.is_variant() && analyzer->strict_dynamic_checks && !(par_type.is_hard_type() && par_type.is_variant())) {
			analyzer->push_error(make_invalid_argument_error(p_function, p_argument_number, par_type, arg_type, true, false, p_argument), p_argument);
		} else {
#ifdef DEBUG_ENABLED
			// Argument can be anything, so this is unsafe (unless the parameter is a hard variant).
			if (!(par_type.is_hard_type() && par_type.is_variant())) {
				analyzer->mark_node_unsafe(p_argument);
				analyzer->parser->push_warning(p_argument, FSWarning::UNSAFE_CALL_ARGUMENT, itos(p_argument_number), "function", p_function, par_type.to_string(), arg_type.to_string_strict());
			}
#endif // DEBUG_ENABLED
		}
	} else if (par_type.is_hard_type() && !analyzer->is_type_compatible(par_type, arg_type, true)) {
		const bool nullable_mismatch = analyzer->strict_null_checks && arg_type.is_nullable && !par_type.is_nullable && !par_type.is_variant();
		String type_handle_error;
		if (!nullable_mismatch) {
			type_handle_error = analyzer->make_type_handle_argument_error(p_function, p_argument_number, par_type, arg_type, p_argument);
		}
		if (!type_handle_error.is_empty()) {
			analyzer->push_error(type_handle_error, p_argument);
		} else if (nullable_mismatch || !FSTypeCompatibility::allows_runtime_narrowing(par_type, arg_type)) {
			analyzer->push_error(make_invalid_argument_error(p_function, p_argument_number, par_type, arg_type, false, nullable_mismatch, p_argument), p_argument);
#ifdef DEBUG_ENABLED
		} else {
			// Supertypes are acceptable for dynamic compliance, but it's unsafe.
			analyzer->mark_node_unsafe(p_call != nullptr ? static_cast<const FSParser::Node *>(p_call) : static_cast<const FSParser::Node *>(p_argument));
			analyzer->parser->push_warning(p_argument, FSWarning::UNSAFE_CALL_ARGUMENT, itos(p_argument_number), "function", p_function, par_type.to_string(), arg_type.to_string_strict());
#endif // DEBUG_ENABLED
		}
#ifdef DEBUG_ENABLED
	} else if (par_type.kind == FSParser::DataType::BUILTIN && par_type.builtin_type == Variant::INT && arg_type.kind == FSParser::DataType::BUILTIN && arg_type.builtin_type == Variant::FLOAT) {
		analyzer->parser->push_warning(p_argument, FSWarning::NARROWING_CONVERSION, p_function);
#endif // DEBUG_ENABLED
	}
}

void FSAnalyzer::CallSiteValidationContext::validate_call_arg(const List<FSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, const FSParser::CallNode *p_call, const Vector<int> &p_extra_allowed_argument_counts, int p_trailing_unbound_argument_count, const FSParser::DataType *p_rest_parameter_type) {
	if (p_call->arguments.size() < p_par_types.size() - p_default_args_count && !_method_signature_accepts_argument_count(p_call->arguments.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_call->function_name, p_par_types.size() - p_default_args_count, p_call->arguments.size()), p_call);
	}
	if (!p_is_vararg && p_call->arguments.size() > p_par_types.size() && !_method_signature_accepts_argument_count(p_call->arguments.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_call->function_name, p_par_types.size(), p_call->arguments.size()), p_call->arguments[p_par_types.size()]);
	}

	const FSParser::DataType *element_type = rest_element_type(p_rest_parameter_type);
	List<FSParser::DataType>::ConstIterator par_itr = p_par_types.begin();
	const int checked_argument_count = MAX(p_call->arguments.size() - p_trailing_unbound_argument_count, 0);
	for (int i = 0; i < checked_argument_count; ++i) {
		const FSParser::DataType *expected_type = nullptr;
		if (i < p_par_types.size()) {
			// A default the analyzer synthesized for a skipped middle parameter is not validated against
			// the (possibly type-parameter-substituted) parameter type, mirroring a trailing omitted
			// default the callee fills in itself. Its value was already coerced to the declared type when
			// the parameter was resolved, so the baked constant matches the callee's runtime default.
			if (!p_call->synthesized_argument_indices.has(i)) {
				expected_type = &*par_itr;
			}
			++par_itr;
		} else {
			// Surplus arguments occupy repeated rest-element slots, so they are checked against the
			// rest array's element type under the same policy as a fixed parameter.
			expected_type = element_type;
		}
		if (expected_type == nullptr) {
			continue;
		}
		validate_argument_against_type(*expected_type, p_call->arguments[i], i + 1, p_call->function_name, p_call);
	}
}

void FSAnalyzer::CallSiteValidationContext::validate_callable_array_literal_args(const Vector<FSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, FSParser::ArrayNode *p_array, const StringName &p_function, const Vector<int> &p_extra_allowed_argument_counts, int p_trailing_unbound_argument_count) {
	if (p_array == nullptr) {
		return;
	}

	if (p_array->elements.size() < p_par_types.size() - p_default_args_count && !_method_signature_accepts_argument_count(p_array->elements.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_function, p_par_types.size() - p_default_args_count, p_array->elements.size()), p_array);
	}
	if (!p_is_vararg && p_array->elements.size() > p_par_types.size() && !_method_signature_accepts_argument_count(p_array->elements.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_function, p_par_types.size(), p_array->elements.size()), p_array->elements[p_par_types.size()]);
	}

	const int checked_argument_count = MAX(p_array->elements.size() - p_trailing_unbound_argument_count, 0);
	for (int i = 0; i < checked_argument_count; i++) {
		if (i >= p_par_types.size()) {
			break;
		}
		validate_argument_against_type(p_par_types[i], p_array->elements[i], i + 1, p_function, nullptr);
	}
}

FSParser::DataType FSAnalyzer::CallSiteValidationContext::explicit_signal_type_from_info(const MethodInfo &p_info) const {
	FSParser::DataType signal_type = make_signal_type(p_info);
	signal_type.method_parameter_types.clear();
	for (const PropertyInfo &argument : p_info.arguments) {
		signal_type.method_parameter_types.push_back(analyzer->type_from_property(argument, true));
	}
	signal_type.has_explicit_method_signature = true;
	return signal_type;
}

FSParser::DataType FSAnalyzer::CallSiteValidationContext::explicit_signal_type_from_node(const FSParser::SignalNode *p_signal) const {
	FSParser::DataType signal_type = p_signal->get_datatype();
	signal_type.method_parameter_types.clear();
	for (FSParser::ParameterNode *parameter : p_signal->parameters) {
		signal_type.method_parameter_types.push_back(parameter->get_datatype());
	}
	signal_type.has_method_signature = true;
	signal_type.has_explicit_method_signature = true;
	return signal_type;
}

bool FSAnalyzer::CallSiteValidationContext::signal_name_from_constant_arg(const FSParser::CallNode *p_call, int p_signal_arg_index, StringName &r_signal_name) const {
	if (p_signal_arg_index < 0 || p_signal_arg_index >= p_call->arguments.size()) {
		return false;
	}

	const FSParser::ExpressionNode *signal_arg = p_call->arguments[p_signal_arg_index];
	if (signal_arg == nullptr || !signal_arg->is_constant) {
		return false;
	}

	const Variant signal_name_value = signal_arg->reduced_value;
	if (signal_name_value.get_type() != Variant::STRING && signal_name_value.get_type() != Variant::STRING_NAME) {
		return false;
	}

	r_signal_name = signal_name_value;
	return true;
}

bool FSAnalyzer::CallSiteValidationContext::signal_type_from_receiver(const FSParser::DataType &p_receiver_type, const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const {
	if (p_receiver_type.kind == FSParser::DataType::CLASS) {
		return signal_type_from_class_constant_arg(p_receiver_type.class_type, p_call, p_signal_arg_index, r_signal_type);
	}
	if (p_receiver_type.kind == FSParser::DataType::NATIVE) {
		return signal_type_from_native_constant_arg(p_receiver_type.native_type, p_call, p_signal_arg_index, r_signal_type);
	}
	return false;
}

bool FSAnalyzer::CallSiteValidationContext::signal_type_from_class_constant_arg(const FSParser::ClassNode *p_class, const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const {
	if (p_class == nullptr) {
		return false;
	}

	StringName signal_name;
	if (!signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		return false;
	}

	for (const FSParser::ClassNode *class_node = p_class; class_node != nullptr;) {
		if (class_node->has_member(signal_name)) {
			const FSParser::ClassNode::Member &member = class_node->get_member(signal_name);
			if (member.type != FSParser::ClassNode::Member::SIGNAL) {
				return false;
			}

			r_signal_type = explicit_signal_type_from_node(member.signal);
			return true;
		}

		if (class_node->base_type.kind == FSParser::DataType::CLASS) {
			class_node = class_node->base_type.class_type;
		} else if (class_node->base_type.kind == FSParser::DataType::NATIVE) {
			return signal_type_from_native_constant_arg(class_node->base_type.native_type, p_call, p_signal_arg_index, r_signal_type);
		} else {
			class_node = nullptr;
		}
	}

	return false;
}

bool FSAnalyzer::CallSiteValidationContext::signal_type_from_native_constant_arg(const StringName &p_native_type, const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const {
	if (p_native_type == StringName()) {
		return false;
	}

	StringName signal_name;
	if (!signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		return false;
	}

	MethodInfo signal_info;
	if (!ClassDB::get_signal(p_native_type, signal_name, &signal_info)) {
		return false;
	}

	r_signal_type = explicit_signal_type_from_info(signal_info);
	return true;
}

bool FSAnalyzer::CallSiteValidationContext::local_signal_type_from_constant_arg(const FSParser::CallNode *p_call, int p_signal_arg_index, FSParser::DataType &r_signal_type) const {
	return signal_type_from_class_constant_arg(analyzer->parser->current_class, p_call, p_signal_arg_index, r_signal_type);
}

void FSAnalyzer::CallSiteValidationContext::validate_strict_signal_name_fallback(const FSParser::CallNode *p_call, const FSParser::DataType &p_receiver_type, int p_signal_arg_index) {
	if (!analyzer->strict_dynamic_checks || p_call == nullptr || p_signal_arg_index < 0 || p_signal_arg_index >= p_call->arguments.size()) {
		return;
	}

	StringName signal_name;
	if (signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		analyzer->push_error(vformat(R"*(Cannot resolve signal "%s" on type "%s" for "%s()" in strict dynamic mode.)*",
									 signal_name,
									 p_receiver_type.to_string(),
									 p_call->function_name),
				p_call->arguments[p_signal_arg_index]);
	} else if (call_argument_can_be_string_name(p_call, p_signal_arg_index)) {
		analyzer->push_error(vformat(R"*(Cannot use dynamic signal name for "%s()" on type "%s" in strict dynamic mode.)*",
									 p_call->function_name,
									 p_receiver_type.to_string()),
				p_call->arguments[p_signal_arg_index]);
	}
}

void FSAnalyzer::CallSiteValidationContext::validate_signal_connect_arg(const FSParser::DataType &p_signal_type, const FSParser::CallNode *p_call, int p_callable_arg_index) {
	if ((p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) || p_callable_arg_index < 0 || p_callable_arg_index >= p_call->arguments.size()) {
		return;
	}
	if (p_signal_type.kind != FSParser::DataType::BUILTIN || p_signal_type.builtin_type != Variant::SIGNAL || !p_signal_type.has_method_signature) {
		return;
	}
	if (p_signal_type.method_parameter_types.size() != p_signal_type.method_info.arguments.size()) {
		// The rich per-parameter signature was dropped in favor of the MethodInfo form because its slots
		// could not be compared reliably across the script-API boundary. Its (empty) parameter list would
		// make every handler look like an arity mismatch, so leave such a signal unvalidated.
		return;
	}

	const FSParser::DataType callable_type = p_call->arguments[p_callable_arg_index]->get_datatype();
	Vector<FSParser::DataType> callable_parameter_types;
	int callable_default_arg_count = 0;
	bool callable_is_vararg = false;
	if (!callable_signature_from_type(callable_type, callable_parameter_types, callable_default_arg_count, callable_is_vararg)) {
		return;
	}

	const int signal_argument_count = p_signal_type.method_parameter_types.size();
	const int callable_argument_count = callable_parameter_types.size();
	const int callable_min_argument_count = callable_argument_count - callable_default_arg_count;
	const StringName action_name = p_call->function_name == SNAME("disconnect") ? SNAME("disconnect") : (p_call->function_name == SNAME("is_connected") ? StringName("check connection for") : SNAME("connect"));
	const String callable_type_string = callable_type_string_with_signature(callable_type, callable_parameter_types);
	const String signal_type_string = signal_type_string_with_signature(p_signal_type);
	if (!_method_signature_accepts_argument_count(signal_argument_count, callable_argument_count, callable_default_arg_count, callable_is_vararg, callable_type.method_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Cannot %s signal "%s" to callable "%s": signal emits %d arguments but callable expects %s%d.)*",
									 action_name,
									 signal_type_string,
									 callable_type_string,
									 signal_argument_count,
									 callable_default_arg_count > 0 ? "at least " : "",
									 callable_default_arg_count > 0 ? callable_min_argument_count : callable_argument_count),
				p_call->arguments[p_callable_arg_index]);
		return;
	}

	FSTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;
	options.strict_dynamic = true;
	options.strict_null = analyzer->strict_null_checks;

	const int checked_signal_argument_count = MAX(signal_argument_count - callable_type.method_unbound_argument_count, 0);
	for (int i = 0; i < checked_signal_argument_count && i < callable_argument_count; i++) {
		const FSParser::DataType &callable_parameter_type = callable_parameter_types[i];
		const FSParser::DataType &signal_parameter_type = p_signal_type.method_parameter_types[i];
		const bool nullable_mismatch = analyzer->strict_null_checks && signal_parameter_type.is_nullable && !callable_parameter_type.is_nullable && !callable_parameter_type.is_variant();
		if (nullable_mismatch || !FSTypeCompatibility::check(callable_parameter_type, signal_parameter_type, options).compatible) {
			if (nullable_mismatch) {
				analyzer->push_error(vformat("Cannot %s signal \"%s\" to callable \"%s\": signal argument %d is nullable "
											 "type \"%s\", but callable parameter expects non-nullable \"%s\".",
											 action_name,
											 signal_type_string,
											 callable_type_string,
											 i + 1,
											 signal_parameter_type.to_string(),
											 callable_parameter_type.to_string()),
						p_call->arguments[p_callable_arg_index]);
			} else {
				analyzer->push_error(vformat(R"*(Cannot %s signal "%s" to callable "%s": signal argument %d of type "%s" cannot be passed to callable parameter of type "%s".)*",
											 action_name,
											 signal_type_string,
											 callable_type_string,
											 i + 1,
											 signal_parameter_type.to_string(),
											 callable_parameter_type.to_string()),
						p_call->arguments[p_callable_arg_index]);
			}
			return;
		}
	}
}

void FSAnalyzer::CallSiteValidationContext::validate_signal_emit_args(const FSParser::DataType &p_signal_type, const FSParser::CallNode *p_call, int p_first_emit_arg_index) {
	if (p_signal_type.kind != FSParser::DataType::BUILTIN || p_signal_type.builtin_type != Variant::SIGNAL || !p_signal_type.has_method_signature) {
		return;
	}

	const int signal_argument_count = p_signal_type.method_parameter_types.size();
	const int emit_argument_count = p_call->arguments.size() - p_first_emit_arg_index;
	if (emit_argument_count < signal_argument_count) {
		analyzer->push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_call->function_name, signal_argument_count + p_first_emit_arg_index, p_call->arguments.size()), p_call);
		return;
	}
	if (emit_argument_count > signal_argument_count) {
		analyzer->push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_call->function_name, signal_argument_count + p_first_emit_arg_index, p_call->arguments.size()), p_call->arguments[signal_argument_count + p_first_emit_arg_index]);
		return;
	}

	// Refine the per-payload parameter types cached for downstream call handling.
	// The generic `Object.emit_signal` vararg signature recorded Variant payload slots; overwrite
	// them with the resolved signal parameter types, aligned to the call's actual argument indices
	// (the leading name argument occupies the slots before `p_first_emit_arg_index`). The analyzer
	// owns the parsed tree, so writing through the const handle is sound.
	FSParser::CallNode *mutable_call = const_cast<FSParser::CallNode *>(p_call);
	if (mutable_call->resolved_parameter_types.size() < p_first_emit_arg_index + signal_argument_count) {
		mutable_call->resolved_parameter_types.resize(p_first_emit_arg_index + signal_argument_count);
	}
	for (int i = 0; i < signal_argument_count; i++) {
		mutable_call->resolved_parameter_types.write[p_first_emit_arg_index + i] = p_signal_type.method_parameter_types[i];
	}

	FSTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;
	options.strict_dynamic = analyzer->strict_dynamic_checks;
	options.strict_null = analyzer->strict_null_checks;

	for (int i = 0; i < signal_argument_count; i++) {
		const int emit_argument_index = p_first_emit_arg_index + i;
		const FSParser::DataType &signal_parameter_type = p_signal_type.method_parameter_types[i];
		const FSParser::DataType emit_argument_type = p_call->arguments[emit_argument_index]->get_datatype();

		analyzer->mark_coroutine_handle_capture(p_call->arguments[emit_argument_index], signal_parameter_type);

		if (emit_argument_type.has_no_type()) {
			analyzer->mark_node_unsafe(p_call->arguments[emit_argument_index]);
			continue;
		}
		if (emit_argument_type.is_variant() || !emit_argument_type.is_hard_type()) {
			if (emit_argument_type.is_variant() && analyzer->strict_dynamic_checks && !(signal_parameter_type.is_hard_type() && signal_parameter_type.is_variant())) {
				analyzer->push_error(make_invalid_argument_error(
											 p_call->function_name,
											 emit_argument_index + 1,
											 signal_parameter_type,
											 emit_argument_type,
											 true,
											 false,
											 p_call->arguments[emit_argument_index]),
						p_call->arguments[emit_argument_index]);
			} else {
				analyzer->mark_node_unsafe(p_call->arguments[emit_argument_index]);
			}
			continue;
		}

		const bool nullable_mismatch = analyzer->strict_null_checks && emit_argument_type.is_nullable && !signal_parameter_type.is_nullable && !signal_parameter_type.is_variant();
		if (nullable_mismatch || !FSTypeCompatibility::check(signal_parameter_type, emit_argument_type, options).compatible) {
			analyzer->push_error(make_invalid_argument_error(
										 p_call->function_name,
										 emit_argument_index + 1,
										 signal_parameter_type,
										 emit_argument_type,
										 false,
										 nullable_mismatch,
										 p_call->arguments[emit_argument_index]),
					p_call->arguments[emit_argument_index]);
			return;
		}
	}
}

void FSAnalyzer::CallSiteValidationContext::validate_local_object_signal_callable_arg(const FSParser::CallNode *p_call, bool p_is_self) {
	if (!p_is_self || (p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) || p_call->arguments.size() < 2) {
		return;
	}

	FSParser::DataType signal_type;
	if (!local_signal_type_from_constant_arg(p_call, 0, signal_type)) {
		validate_strict_signal_name_fallback(p_call, analyzer->parser->current_class->get_datatype(), 0);
		return;
	}

	validate_signal_connect_arg(signal_type, p_call, 1);
}

void FSAnalyzer::CallSiteValidationContext::validate_local_object_emit_signal_args(const FSParser::CallNode *p_call, bool p_is_self) {
	if (!p_is_self || p_call->function_name != SNAME("emit_signal") || p_call->arguments.is_empty()) {
		return;
	}

	FSParser::DataType signal_type;
	if (!local_signal_type_from_constant_arg(p_call, 0, signal_type)) {
		validate_strict_signal_name_fallback(p_call, analyzer->parser->current_class->get_datatype(), 0);
		return;
	}

	validate_signal_emit_args(signal_type, p_call, 1);
}

void FSAnalyzer::CallSiteValidationContext::validate_typed_object_signal_api_args(const FSParser::DataType &p_base_type, const FSParser::CallNode *p_call, bool p_is_self) {
	if (p_is_self) {
		return;
	}

	if (p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected") && p_call->function_name != SNAME("emit_signal")) {
		return;
	}

	if (p_base_type.kind != FSParser::DataType::CLASS && p_base_type.kind != FSParser::DataType::NATIVE) {
		return;
	}

	FSParser::DataType signal_type;
	if (!signal_type_from_receiver(p_base_type, p_call, 0, signal_type)) {
		validate_strict_signal_name_fallback(p_call, p_base_type, 0);
		return;
	}

	if (p_call->function_name == SNAME("emit_signal")) {
		validate_signal_emit_args(signal_type, p_call, 1);
	} else {
		validate_signal_connect_arg(signal_type, p_call, 1);
	}
}
