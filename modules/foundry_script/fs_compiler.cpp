/**************************************************************************/
/*  fs_compiler.cpp                                                       */
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

#include "fs_compiler.h"

#include "foundry_script.h"
#include "fs_analyzer.h"
#include "fs_byte_codegen.h"
#include "fs_cache.h"
#include "fs_trait_utils.h"
#include "fs_utility_functions.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"

#include "scene/scene_string_names.h"

bool FSCompiler::_is_class_member_property(CodeGen &codegen, const StringName &p_name) {
	if (codegen.function_node && codegen.function_node->is_static) {
		return false;
	}

	if (_is_local_or_parameter(codegen, p_name)) {
		return false; //shadowed
	}

	return _is_class_member_property(codegen.script, p_name);
}

bool FSCompiler::_is_class_member_property(FoundryScript *owner, const StringName &p_name) {
	FoundryScript *scr = owner;
	FSNativeClass *nc = nullptr;
	while (scr) {
		if (scr->native.is_valid()) {
			nc = scr->native.ptr();
		}
		scr = scr->base.ptr();
	}

	ERR_FAIL_NULL_V(nc, false);

	return ClassDB::has_property(nc->get_name(), p_name);
}

bool FSCompiler::_is_local_or_parameter(CodeGen &codegen, const StringName &p_name) {
	return codegen.parameters.has(p_name) || codegen.locals.has(p_name);
}

static FSParser::DataType _class_type_parameter_handle(const FSParser::TypeParameterNode *p_parameter, int p_index) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::TYPE_PARAMETER;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	if (p_parameter != nullptr && p_parameter->identifier != nullptr) {
		type.type_parameter_name = p_parameter->identifier->name;
	}
	type.type_parameter_scope = FSParser::DataType::TYPE_PARAMETER_CLASS;
	type.type_parameter_index = p_index;
	if (p_parameter != nullptr && !p_parameter->resolved_bound.has_no_type()) {
		type.type_parameter_bound.push_back(p_parameter->resolved_bound);
	}
	return type;
}

static FSParser::DataType _self_type_for_class(const FSParser::ClassNode *p_class) {
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

static FSParser::DataType _substitute_self_type_parameter_for_class(
		const FSParser::DataType &p_type,
		const FSParser::ClassNode *p_class) {
	FSParser::DataType self_type = _self_type_for_class(p_class);
	if (!self_type.is_set()) {
		return p_type;
	}
	HashMap<StringName, FSParser::DataType> bindings;
	bindings.insert(SNAME("@Self"), self_type);
	return FSParser::DataType::substitute(p_type, bindings);
}

void FSCompiler::_set_error(const String &p_error, const FSParser::Node *p_node) {
	if (!error.is_empty()) {
		return;
	}

	error = p_error;
	if (p_node) {
		err_line = p_node->start_line;
		err_column = p_node->start_column;
	} else {
		err_line = 0;
		err_column = 0;
	}
}

static bool _datatype_contains_erased_type_parameter(const FSParser::DataType &p_datatype) {
	if (p_datatype.kind == FSParser::DataType::TYPE_PARAMETER) {
		return p_datatype.type_parameter_name != SNAME("@Self");
	}
	for (const FSParser::DataType &element : p_datatype.container_element_types) {
		if (_datatype_contains_erased_type_parameter(element)) {
			return true;
		}
	}
	for (const FSParser::DataType &argument : p_datatype.type_arguments) {
		if (_datatype_contains_erased_type_parameter(argument)) {
			return true;
		}
	}
	return false;
}

static bool _constant_type_argument_from_expression(const FSParser::ExpressionNode *p_expression,
		FSParser::DataType &r_type_argument) {
	if (p_expression == nullptr) {
		return false;
	}

	const FSParser::DataType expression_type = p_expression->get_datatype();
	if (expression_type.is_meta_type || expression_type.is_type_handle_annotation) {
		r_type_argument = FSAnalyzer::type_from_metatype(expression_type);
		return true;
	}
	if (expression_type.kind == FSParser::DataType::TYPE_PARAMETER) {
		r_type_argument = expression_type;
		return true;
	}
	if (p_expression->type == FSParser::Node::IDENTIFIER) {
		const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
		const Variant::Type builtin_type = FSParser::get_builtin_type(identifier->name);
		if (builtin_type < Variant::VARIANT_MAX) {
			r_type_argument.kind = FSParser::DataType::BUILTIN;
			r_type_argument.builtin_type = builtin_type;
			r_type_argument.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
			return true;
		}
	}
	if (p_expression->type != FSParser::Node::SUBSCRIPT) {
		return false;
	}

	const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
	if (subscript->is_attribute || subscript->base == nullptr || subscript->index == nullptr) {
		return false;
	}

	FSParser::DataType base_argument;
	if (!_constant_type_argument_from_expression(subscript->base, base_argument) ||
			base_argument.kind != FSParser::DataType::BUILTIN) {
		return false;
	}

	LocalVector<const FSParser::ExpressionNode *> element_expressions;
	if (subscript->type_arguments.is_empty()) {
		element_expressions.push_back(subscript->index);
	} else {
		for (const FSParser::ExpressionNode *argument : subscript->type_arguments) {
			element_expressions.push_back(argument);
		}
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

	for (uint32_t i = 0; i < element_expressions.size(); i++) {
		FSParser::DataType element_argument;
		if (!_constant_type_argument_from_expression(element_expressions[i], element_argument)) {
			return false;
		}
		base_argument.set_container_element_type(i, element_argument);
	}

	r_type_argument = base_argument;
	return true;
}

static bool _specialized_class_handle_datatype_from_expression(const FSParser::ExpressionNode *p_expression,
		FSParser::DataType &r_datatype) {
	if (p_expression == nullptr || p_expression->type != FSParser::Node::SUBSCRIPT) {
		return false;
	}

	const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
	if (subscript->is_attribute || subscript->base == nullptr || subscript->index == nullptr) {
		return false;
	}

	FSParser::DataType specialized = subscript->base->get_datatype();
	if (!specialized.is_meta_type || specialized.kind != FSParser::DataType::CLASS ||
			specialized.class_type == nullptr || specialized.class_type->type_parameters.is_empty()) {
		return false;
	}

	LocalVector<const FSParser::ExpressionNode *> argument_expressions;
	if (subscript->type_arguments.is_empty()) {
		argument_expressions.push_back(subscript->index);
	} else {
		for (const FSParser::ExpressionNode *argument : subscript->type_arguments) {
			argument_expressions.push_back(argument);
		}
	}
	if (argument_expressions.size() != (uint32_t)specialized.class_type->type_parameters.size()) {
		return false;
	}

	specialized.type_arguments.clear();
	for (const FSParser::ExpressionNode *argument_expression : argument_expressions) {
		FSParser::DataType argument_type;
		if (!_constant_type_argument_from_expression(argument_expression, argument_type)) {
			return false;
		}
		specialized.type_arguments.push_back(argument_type);
	}
	specialized.is_meta_type = true;
	r_datatype = specialized;
	return true;
}

static FSParser::DataType _constant_storage_datatype(const FSParser::ConstantNode *p_constant) {
	const FSParser::DataType declared_type = p_constant->get_datatype();
	if (p_constant->datatype_specifier != nullptr && declared_type.is_hard_type() && !declared_type.is_variant()) {
		return declared_type;
	}
	if (p_constant->datatype_specifier != nullptr && declared_type.is_variant()) {
		FSParser::DataType specialized_type;
		if (_specialized_class_handle_datatype_from_expression(p_constant->initializer, specialized_type)) {
			return specialized_type;
		}
	}
	return p_constant->initializer->get_datatype();
}

static bool _datatype_contains_coroutine(const FSParser::DataType &p_datatype) {
	if (p_datatype.is_coroutine) {
		return true;
	}
	for (const FSParser::DataType &element : p_datatype.container_element_types) {
		if (_datatype_contains_coroutine(element)) {
			return true;
		}
	}
	for (const FSParser::DataType &argument : p_datatype.type_arguments) {
		if (_datatype_contains_coroutine(argument)) {
			return true;
		}
	}
	return false;
}

FSDataType FSCompiler::_gdtype_from_datatype(const FSParser::DataType &p_datatype, FoundryScript *p_owner, bool p_handle_metatype) {
	if (!p_datatype.is_set() || !p_datatype.is_hard_type() || p_datatype.is_coroutine) {
		return FSDataType();
	}

	FSDataType result;

	switch (p_datatype.kind) {
		case FSParser::DataType::VARIANT: {
			result.kind = FSDataType::VARIANT;
		} break;
		case FSParser::DataType::BUILTIN: {
			result.kind = FSDataType::BUILTIN;
			result.builtin_type = p_datatype.builtin_type;
		} break;
		case FSParser::DataType::NATIVE: {
			if (p_handle_metatype && p_datatype.is_meta_type && !p_datatype.is_type_handle_annotation) {
				result.kind = FSDataType::NATIVE;
				result.builtin_type = Variant::OBJECT;
				// Fixes GH-82255. `FSNativeClass` is obtainable in FoundryScript,
				// but is not a registered and exposed class, so `FSNativeClass`
				// is missing from `FSLanguage::get_singleton()->get_global_map()`.
				//result.native_type = FSNativeClass::get_class_static();
				result.native_type = Object::get_class_static();
				break;
			}

			result.kind = FSDataType::NATIVE;
			result.builtin_type = p_datatype.builtin_type;
			result.native_type = p_datatype.native_type;

#ifdef DEBUG_ENABLED
			if (unlikely(!FSLanguage::get_singleton()->get_global_map().has(result.native_type))) {
				_set_error(vformat(R"(FoundryScript bug (please report): Native class "%s" not found.)", result.native_type), nullptr);
				return FSDataType();
			}
#endif
		} break;
		case FSParser::DataType::SCRIPT: {
			if (p_handle_metatype && p_datatype.is_meta_type && !p_datatype.is_type_handle_annotation) {
				result.kind = FSDataType::NATIVE;
				result.builtin_type = Variant::OBJECT;
				result.native_type = p_datatype.script_type.is_valid() ? p_datatype.script_type->get_class_name() : Script::get_class_static();
				break;
			}

			result.kind = FSDataType::SCRIPT;
			result.builtin_type = p_datatype.builtin_type;
			result.script_type_ref = p_datatype.script_type;
			result.script_type = result.script_type_ref.ptr();
			result.native_type = p_datatype.native_type;
		} break;
		case FSParser::DataType::CLASS: {
			if (p_handle_metatype && p_datatype.is_meta_type && !p_datatype.is_type_handle_annotation) {
				result.kind = FSDataType::NATIVE;
				result.builtin_type = Variant::OBJECT;
				result.native_type = FoundryScript::get_class_static();
				break;
			}

			result.kind = FSDataType::FOUNDRY_SCRIPT;
			result.builtin_type = p_datatype.builtin_type;
			result.native_type = p_datatype.native_type;

			bool is_local_class = parser->has_class(p_datatype.class_type);

			Ref<FoundryScript> script;
			if (is_local_class) {
				script = Ref<FoundryScript>(main_script);
			} else {
				Error err = OK;
				script = FSCache::get_shallow_script(p_datatype.script_path, err, p_owner->path);
				if (err) {
					_set_error(vformat(R"(Could not find script "%s": %s)", p_datatype.script_path, error_names[err]), nullptr);
					return FSDataType();
				}
			}

			if (script.is_valid()) {
				script = Ref<FoundryScript>(script->find_class(p_datatype.class_type->fqcn));
			}

			if (script.is_null()) {
				_set_error(vformat(R"(Could not find class "%s" in "%s".)", p_datatype.class_type->fqcn, p_datatype.script_path), nullptr);
				return FSDataType();
			} else {
				// Only hold a strong reference if the owner of the element qualified with this type is not local, to avoid cyclic references (leaks).
				// TODO: Might lead to use after free if script_type is a subclass and is used after its parent is freed.
				if (!is_local_class) {
					result.script_type_ref = script;
				}
				result.script_type = script.ptr();
				result.native_type = p_datatype.native_type;
				if (p_datatype.class_type->is_trait) {
					result.is_script_trait = true;
					result.script_trait = fs_trait_identity_name(p_datatype.class_type);
					script->_is_trait_type = true;
					script->trait_type_name = result.script_trait;
				}
			}
		} break;
		case FSParser::DataType::ENUM:
			if (p_handle_metatype && p_datatype.is_meta_type) {
				result.kind = FSDataType::BUILTIN;
				result.builtin_type = Variant::DICTIONARY;
				break;
			}

			result.kind = FSDataType::BUILTIN;
			result.builtin_type = p_datatype.builtin_type;
			break;
		case FSParser::DataType::TYPE_PARAMETER: {
			if (p_datatype.type_parameter_name == SNAME("@Self") && p_owner != nullptr) {
				result.kind = FSDataType::FOUNDRY_SCRIPT;
				result.builtin_type = Variant::OBJECT;
				result.script_type = p_owner;
				result.native_type = p_owner->get_instance_base_type();
				result.is_self_type = true;
				break;
			}
			// Plain `T` is erased to Variant. `Type[T]` cannot preserve the represented method parameter
			// at runtime, but it can still enforce that the value is a class handle.
			if (p_handle_metatype && p_datatype.is_type_handle_annotation) {
				result.kind = FSDataType::NATIVE;
				result.builtin_type = Variant::OBJECT;
				result.native_type = Object::get_class_static();
			} else {
				result.kind = FSDataType::VARIANT;
			}
		} break;
		case FSParser::DataType::RESOLVING:
		case FSParser::DataType::UNRESOLVED: {
			_set_error("Parser bug (please report): converting unresolved type.", nullptr);
			return FSDataType();
		}
	}

	result.is_type_handle = p_handle_metatype && p_datatype.is_type_handle_annotation;

	// A nullable value type must accept null at runtime. Metatypes are never nullable.
	result.is_nullable = p_datatype.is_nullable && !(p_handle_metatype && p_datatype.is_meta_type);

	// A container whose element type (transitively) involves an erased type parameter — `Array[T]`,
	// `Dictionary[K, int]`, `Array[Array[T]]` — or a coroutine leaves all of its element types unset so
	// the runtime treats it as a fully untyped container. The VM compares typed-container element
	// metadata exactly (and a partially-set typed container backfills the missing slot with Variant), so
	// a typed-but-erased element would still reject a concrete argument like `Array[int]`. Synthetic
	// `@Self` is reified against the owner script, so it can preserve runtime metadata. The analyzer
	// still enforces element types statically.
	bool erases_container_element = false;
	for (int i = 0; i < p_datatype.container_element_types.size(); i++) {
		const FSParser::DataType element = p_datatype.get_container_element_type_or_variant(i);
		// Coroutine[T] is a phantom type whose runtime value is a FSFunctionState, so a
		// container of coroutines (`Array[Coroutine[String]]`) erases its element type to stay an
		// untyped container at runtime, matching erased type-parameter handling. Synthetic `@Self`
		// stays reified against the owner script and can preserve runtime metadata.
		if (_datatype_contains_erased_type_parameter(element) || _datatype_contains_coroutine(element)) {
			erases_container_element = true;
			break;
		}
	}
	if (!erases_container_element) {
		for (int i = 0; i < p_datatype.container_element_types.size(); i++) {
			FSDataType element_type = _gdtype_from_datatype(p_datatype.get_container_element_type_or_variant(i), p_owner, false);
			if (element_type.is_nullable) {
				// Core typed containers cannot hold null elements, so a nullable element type becomes an
				// untyped element. The analyzer still enforces element types statically.
				element_type = FSDataType();
			}
			result.set_container_element_type(i, element_type);
		}
	}

	// Preserve specialized type arguments (e.g. the `int` in `Box[int]`) so runtime metadata is not lost.
	for (int i = 0; i < p_datatype.type_arguments.size(); i++) {
		result.type_arguments.push_back(_gdtype_from_datatype(p_datatype.type_arguments[i], p_owner, false));
	}

	return result;
}

static void _rebind_self_data_type(FSDataType &p_type, FoundryScript *p_owner) {
	if (p_owner == nullptr) {
		return;
	}
	if (p_type.is_self_type) {
		p_type.kind = FSDataType::FOUNDRY_SCRIPT;
		p_type.builtin_type = Variant::OBJECT;
		p_type.script_type = p_owner;
		p_type.script_type_ref = Ref<Script>();
		p_type.native_type = p_owner->get_instance_base_type();
		p_type.is_script_trait = p_owner->is_trait_type();
		p_type.script_trait = p_owner->get_trait_type_name();
	}
	for (FSDataType &element_type : p_type.container_element_types) {
		_rebind_self_data_type(element_type, p_owner);
	}
	for (FSDataType &argument_type : p_type.type_arguments) {
		_rebind_self_data_type(argument_type, p_owner);
	}
}

// Some typed-container calls need a converting retype rather than the strict validate that an ordinary
// typed-array assignment emits: generic method `Array[T]` returns are runtime-erased, and inherited
// `Array[Self]` returns are compiled with the declaring class while statically receiver-specialized.
static bool _is_erased_container_call_to_typed_array(const FSParser::ExpressionNode *p_source, const FSDataType &p_target_type) {
	return p_source != nullptr && p_source->type == FSParser::Node::CALL &&
			static_cast<const FSParser::CallNode *>(p_source)->returns_erased_container &&
			p_target_type.kind == FSDataType::BUILTIN && p_target_type.builtin_type == Variant::ARRAY &&
			p_target_type.has_container_element_type(0);
}

// Same as above for typed dictionaries.
static bool _is_erased_container_call_to_typed_dictionary(const FSParser::ExpressionNode *p_source, const FSDataType &p_target_type) {
	return p_source != nullptr && p_source->type == FSParser::Node::CALL &&
			static_cast<const FSParser::CallNode *>(p_source)->returns_erased_container &&
			p_target_type.kind == FSDataType::BUILTIN && p_target_type.builtin_type == Variant::DICTIONARY &&
			p_target_type.has_container_element_types();
}

static bool _is_exact_type(const PropertyInfo &p_par_type, const FSDataType &p_arg_type) {
	if (!p_arg_type.has_type()) {
		return false;
	}
	if (p_par_type.type == Variant::NIL) {
		return false;
	}
	if (p_par_type.type == Variant::OBJECT) {
		if (p_arg_type.kind == FSDataType::BUILTIN) {
			return false;
		}
		StringName class_name;
		if (p_arg_type.kind == FSDataType::NATIVE) {
			class_name = p_arg_type.native_type;
		} else {
			class_name = p_arg_type.native_type == StringName() ? p_arg_type.script_type->get_instance_base_type() : p_arg_type.native_type;
		}
		return p_par_type.class_name == class_name || ClassDB::is_parent_class(class_name, p_par_type.class_name);
	} else {
		if (p_arg_type.kind != FSDataType::BUILTIN) {
			return false;
		}
		return p_par_type.type == p_arg_type.builtin_type;
	}
}

static bool _can_use_validate_call(const MethodBind *p_method, const Vector<FSCodeGenerator::Address> &p_arguments) {
	if (p_method->is_vararg()) {
		// Validated call won't work with vararg methods.
		return false;
	}
	if (p_method->get_argument_count() != p_arguments.size()) {
		// Validated call won't work with default arguments.
		return false;
	}
	MethodInfo info;
	ClassDB::get_method_info(p_method->get_instance_class(), p_method->get_name(), &info);
	for (int64_t i = 0; i < info.arguments.size(); ++i) {
		if (!_is_exact_type(info.arguments[i], p_arguments[i].type)) {
			return false;
		}
	}
	return true;
}

FSCodeGenerator::Address FSCompiler::_emit_global_class_value(CodeGen &codegen, Error &r_error, const StringName &p_global_class, const FSParser::ExpressionNode *p_source) {
	const FSParser::ClassNode *class_node = codegen.class_node;
	while (class_node->outer) {
		class_node = class_node->outer;
	}

	Ref<Resource> res;

	// A reference to the class being compiled (by its own name or qualified name)
	// resolves to the live main script instead of loading a separate copy.
	bool is_self_reference = false;
	if (class_node->identifier) {
		if (!class_node->qualified_global_name.is_empty()) {
			is_self_reference = class_node->qualified_global_name == p_global_class;
		} else {
			is_self_reference = class_node->identifier->name == p_global_class;
		}
	}

	if (is_self_reference) {
		res = Ref<FoundryScript>(main_script);
	} else {
		String global_class_path = ScriptServer::get_global_class_path(p_global_class);
		if (ResourceLoader::get_resource_type(global_class_path) == "FoundryScript") {
			Error err = OK;
			// Should not need to pass p_owner since analyzer will already have done it.
			res = FSCache::get_shallow_script(global_class_path, err);
			if (err != OK) {
				_set_error("Can't load global class " + String(p_global_class), p_source);
				r_error = ERR_COMPILATION_FAILED;
				return FSCodeGenerator::Address();
			}
		} else {
			res = ResourceLoader::load(global_class_path);
			if (res.is_null()) {
				_set_error("Can't load global class " + String(p_global_class) + ", cyclic reference?", p_source);
				r_error = ERR_COMPILATION_FAILED;
				return FSCodeGenerator::Address();
			}
		}
	}

	return codegen.add_constant(res);
}

FSCodeGenerator::Address FSCompiler::_parse_expression(CodeGen &codegen, Error &r_error, const FSParser::ExpressionNode *p_expression, bool p_root, bool p_initializer) {
	// A namespaced global script class used as a value (`Foo` from the current or an
	// imported namespace, or a qualified `ns.Foo`). The analyzer resolved the dotted
	// identity, which a bare-name lookup in the identifier/subscript paths below cannot
	// match, so emit the class object directly from the resolved name.
	if (!p_expression->resolved_global_class.is_empty()) {
		return _emit_global_class_value(codegen, r_error, p_expression->resolved_global_class, p_expression);
	}

	const bool constant_foundry_script_handle = p_expression->reduced_value.get_type() == Variant::OBJECT &&
			Object::cast_to<FoundryScript>(p_expression->reduced_value.operator Object *()) != nullptr;
	if (p_expression->is_constant && !constant_foundry_script_handle &&
			!(p_expression->get_datatype().is_meta_type &&
					p_expression->get_datatype().kind == FSParser::DataType::CLASS)) {
		return codegen.add_constant(p_expression->reduced_value);
	}

	FSCodeGenerator *gen = codegen.generator;

	switch (p_expression->type) {
		case FSParser::Node::IDENTIFIER: {
			// Look for identifiers in current scope.
			const FSParser::IdentifierNode *in = static_cast<const FSParser::IdentifierNode *>(p_expression);

			StringName identifier = in->name;

			switch (in->source) {
				// LOCALS.
				case FSParser::IdentifierNode::FUNCTION_PARAMETER:
				case FSParser::IdentifierNode::LOCAL_VARIABLE:
				case FSParser::IdentifierNode::LOCAL_CONSTANT:
				case FSParser::IdentifierNode::LOCAL_ITERATOR:
				case FSParser::IdentifierNode::LOCAL_BIND: {
					// Try function parameters.
					if (codegen.parameters.has(identifier)) {
						return codegen.parameters[identifier];
					}

					// Try local variables and constants.
					if (!p_initializer && codegen.locals.has(identifier)) {
						return codegen.locals[identifier];
					}
				} break;

				// MEMBERS.
				case FSParser::IdentifierNode::MEMBER_VARIABLE:
				case FSParser::IdentifierNode::MEMBER_FUNCTION:
				case FSParser::IdentifierNode::MEMBER_SIGNAL:
				case FSParser::IdentifierNode::INHERITED_VARIABLE: {
					// Try class members.
					if (_is_class_member_property(codegen, identifier)) {
						// Get property.
						FSCodeGenerator::Address temp = codegen.add_temporary(_gdtype_from_datatype(p_expression->get_datatype(), codegen.script));
						gen->write_get_member(temp, identifier);
						return temp;
					}

					// Try members.
					if (!codegen.function_node || !codegen.function_node->is_static) {
						// Try member variables.
						if (codegen.script->member_indices.has(identifier)) {
							if (codegen.script->member_indices[identifier].getter != StringName() && codegen.script->member_indices[identifier].getter != codegen.function_name) {
								// Perform getter.
								FSCodeGenerator::Address temp = codegen.add_temporary(codegen.script->member_indices[identifier].data_type);
								Vector<FSCodeGenerator::Address> args; // No argument needed.
								gen->write_call_self(temp, codegen.script->member_indices[identifier].getter, args);
								return temp;
							} else {
								// No getter or inside getter: direct member access.
								int idx = codegen.script->member_indices[identifier].index;
								return FSCodeGenerator::Address(FSCodeGenerator::Address::MEMBER, idx, codegen.script->get_member_type(identifier));
							}
						}
					}

					// Try methods and signals (can be Callable and Signal).
					{
						// Search upwards through parent classes, including the members each
						// class flattens in from applied traits (functions and signals are
						// referenced by name off `self`/`class` at runtime, where the trait
						// member has been flattened into the script).
						const FSParser::ClassNode *base_class = codegen.class_node;
						while (base_class != nullptr) {
							bool found_member = false;
							FSParser::ClassNode::Member member;
							if (base_class->has_member(identifier)) {
								member = base_class->get_member(identifier);
								found_member = true;
							} else {
								for (FSParser::ClassNode *trait : base_class->resolved_traits) {
									if (trait != nullptr && trait->has_member(identifier)) {
										member = trait->get_member(identifier);
										found_member = true;
										break;
									}
								}
							}

							if (found_member && (member.type == FSParser::ClassNode::Member::FUNCTION || member.type == FSParser::ClassNode::Member::SIGNAL)) {
								// Get like it was a property.
								FSCodeGenerator::Address temp = codegen.add_temporary(); // TODO: Get type here.

								FSCodeGenerator::Address base(FSCodeGenerator::Address::SELF);
								if (member.type == FSParser::ClassNode::Member::FUNCTION && member.function->is_static) {
									base = FSCodeGenerator::Address(FSCodeGenerator::Address::CLASS);
								}

								gen->write_get_named(temp, identifier, base);
								return temp;
							}
							base_class = base_class->base_type.class_type;
						}

						// Try in native base.
						FoundryScript *scr = codegen.script;
						FSNativeClass *nc = nullptr;
						while (scr) {
							if (scr->native.is_valid()) {
								nc = scr->native.ptr();
							}
							scr = scr->base.ptr();
						}

						if (nc && (identifier == CoreStringName(free_) || ClassDB::has_signal(nc->get_name(), identifier) || ClassDB::has_method(nc->get_name(), identifier))) {
							// Get like it was a property.
							FSCodeGenerator::Address temp = codegen.add_temporary(); // TODO: Get type here.
							FSCodeGenerator::Address self(FSCodeGenerator::Address::SELF);

							gen->write_get_named(temp, identifier, self);
							return temp;
						}
					}
				} break;
				case FSParser::IdentifierNode::MEMBER_CONSTANT:
				case FSParser::IdentifierNode::MEMBER_CLASS: {
					// Try class constants.
					FoundryScript *owner = codegen.script;
					while (owner) {
						FoundryScript *scr = owner;
						FSNativeClass *nc = nullptr;

						while (scr) {
							if (scr->constants.has(identifier)) {
								return codegen.add_constant(scr->constants[identifier]); // TODO: Get type here.
							}
							if (scr->native.is_valid()) {
								nc = scr->native.ptr();
							}
							scr = scr->base.ptr();
						}

						// Class C++ integer constant.
						if (nc) {
							bool success = false;
							int64_t constant = ClassDB::get_integer_constant(nc->get_name(), identifier, &success);
							if (success) {
								return codegen.add_constant(constant);
							}
						}

						owner = owner->_owner;
					}
				} break;
				case FSParser::IdentifierNode::STATIC_VARIABLE: {
					// Try static variables.
					FoundryScript *scr = codegen.script;
					while (scr) {
						if (scr->static_variables_indices.has(identifier)) {
							if (scr->static_variables_indices[identifier].getter != StringName() && scr->static_variables_indices[identifier].getter != codegen.function_name) {
								// Perform getter.
								FSCodeGenerator::Address temp = codegen.add_temporary(scr->static_variables_indices[identifier].data_type);
								FSCodeGenerator::Address class_addr(FSCodeGenerator::Address::CLASS);
								Vector<FSCodeGenerator::Address> args; // No argument needed.
								gen->write_call(temp, class_addr, scr->static_variables_indices[identifier].getter, args);
								return temp;
							} else {
								// No getter or inside getter: direct variable access.
								FSCodeGenerator::Address temp = codegen.add_temporary(scr->static_variables_indices[identifier].data_type);
								FSCodeGenerator::Address _class = codegen.add_constant(scr);
								int index = scr->static_variables_indices[identifier].index;
								gen->write_get_static_variable(temp, _class, index);
								return temp;
							}
						}
						scr = scr->base.ptr();
					}
				} break;

				// GLOBALS.
				case FSParser::IdentifierNode::NATIVE_CLASS:
				case FSParser::IdentifierNode::UNDEFINED_SOURCE: {
					// Try globals.
					if (FSLanguage::get_singleton()->get_global_map().has(identifier)) {
						// If it's an autoload singleton, we postpone to load it at runtime.
						// This is so one autoload doesn't try to load another before it's compiled.
						HashMap<StringName, ProjectSettings::AutoloadInfo> autoloads = ProjectSettings::get_singleton()->get_autoload_list();
						if (autoloads.has(identifier) && autoloads[identifier].is_singleton) {
							FSCodeGenerator::Address global = codegen.add_temporary(_gdtype_from_datatype(in->get_datatype(), codegen.script));
							int idx = FSLanguage::get_singleton()->get_global_map()[identifier];
							gen->write_store_global(global, idx);
							return global;
						} else {
							int idx = FSLanguage::get_singleton()->get_global_map()[identifier];
							Variant global = FSLanguage::get_singleton()->get_global_array()[idx];
							return codegen.add_constant(global);
						}
					}

					// Try global classes.
					if (ScriptServer::is_global_class(identifier)) {
						return _emit_global_class_value(codegen, r_error, identifier, p_expression);
					}

#ifdef TOOLS_ENABLED
					if (FSLanguage::get_singleton()->get_named_globals_map().has(identifier)) {
						FSCodeGenerator::Address global = codegen.add_temporary(); // TODO: Get type.
						gen->write_store_named_global(global, identifier);
						return global;
					}
#endif

				} break;
			}

			// Not found, error.
			_set_error("Identifier not found: " + String(identifier), p_expression);
			r_error = ERR_COMPILATION_FAILED;
			return FSCodeGenerator::Address();
		} break;
		case FSParser::Node::LITERAL: {
			// Return constant.
			const FSParser::LiteralNode *cn = static_cast<const FSParser::LiteralNode *>(p_expression);

			const FSParser::DataType &literal_type = cn->get_datatype();
			if (literal_type.is_meta_type && literal_type.kind == FSParser::DataType::CLASS) {
				// A class-metatype literal is only ever synthesized for a named-call middle gap fill,
				// which bakes the analyzer's reduced class object. That object can be a shallow,
				// uncompiled same-unit class, so re-point it to the live compiled subclass here, the
				// same resolution class-constant identifiers and `const` aliases use. An external class
				// is already a compiled, valid class and is left untouched.
				return codegen.add_constant(_resolve_aliased_class_constant(cn->value, literal_type, codegen.script));
			}

			return codegen.add_constant(cn->value);
		} break;
		case FSParser::Node::SELF: {
			//return constant
			if (codegen.function_node && codegen.function_node->is_static) {
				_set_error("'self' not present in static function.", p_expression);
				r_error = ERR_COMPILATION_FAILED;
				return FSCodeGenerator::Address();
			}
			return FSCodeGenerator::Address(FSCodeGenerator::Address::SELF);
		} break;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *an = static_cast<const FSParser::ArrayNode *>(p_expression);
			Vector<FSCodeGenerator::Address> values;

			// Create the result temporary first since it's the last to be killed.
			FSDataType array_type = _gdtype_from_datatype(an->get_datatype(), codegen.script);
			FSCodeGenerator::Address result = codegen.add_temporary(array_type);

			for (int i = 0; i < an->elements.size(); i++) {
				FSCodeGenerator::Address val = _parse_expression(codegen, r_error, an->elements[i]);
				if (r_error) {
					return FSCodeGenerator::Address();
				}
				values.push_back(val);
			}

			if (array_type.has_container_element_type(0)) {
				gen->write_construct_typed_array(result, array_type.get_container_element_type(0), values);
			} else {
				gen->write_construct_array(result, values);
			}

			for (int i = 0; i < values.size(); i++) {
				if (values[i].mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}
			}

			return result;
		} break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dn = static_cast<const FSParser::DictionaryNode *>(p_expression);
			Vector<FSCodeGenerator::Address> elements;

			// Create the result temporary first since it's the last to be killed.
			FSDataType dict_type = _gdtype_from_datatype(dn->get_datatype(), codegen.script);
			FSCodeGenerator::Address result = codegen.add_temporary(dict_type);

			for (int i = 0; i < dn->elements.size(); i++) {
				// Key.
				FSCodeGenerator::Address element;
				switch (dn->style) {
					case FSParser::DictionaryNode::PYTHON_DICT:
						// Python-style: key is any expression.
						element = _parse_expression(codegen, r_error, dn->elements[i].key);
						if (r_error) {
							return FSCodeGenerator::Address();
						}
						break;
					case FSParser::DictionaryNode::LUA_TABLE:
						// Lua-style: key is an identifier interpreted as StringName.
						StringName key = dn->elements[i].key->reduced_value.operator StringName();
						element = codegen.add_constant(key);
						break;
				}

				elements.push_back(element);

				element = _parse_expression(codegen, r_error, dn->elements[i].value);
				if (r_error) {
					return FSCodeGenerator::Address();
				}

				elements.push_back(element);
			}

			if (dict_type.has_container_element_types()) {
				gen->write_construct_typed_dictionary(result, dict_type.get_container_element_type_or_variant(0), dict_type.get_container_element_type_or_variant(1), elements);
			} else {
				gen->write_construct_dictionary(result, elements);
			}

			for (int i = 0; i < elements.size(); i++) {
				if (elements[i].mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}
			}

			return result;
		} break;
		case FSParser::Node::CAST: {
			const FSParser::CastNode *cn = static_cast<const FSParser::CastNode *>(p_expression);
			const bool handles_type_annotation = cn->get_datatype().is_type_handle_annotation;
			FSDataType cast_type = _gdtype_from_datatype(cn->get_datatype(), codegen.script, handles_type_annotation);

			FSCodeGenerator::Address result;
			if (cast_type.has_type()) {
				// Create temporary for result first since it will be deleted last.
				result = codegen.add_temporary(cast_type);

				FSCodeGenerator::Address src = _parse_expression(codegen, r_error, cn->operand);

				gen->write_cast(result, src, cast_type);

				if (src.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}
			} else {
				result = _parse_expression(codegen, r_error, cn->operand);
			}

			return result;
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
			// Compile the call as async (store the live function-state handle without suspending and
			// skip the debug missing-await guard) when it is the operand of an `await`, or when the
			// analyzer marked its result as captured into a statically `Coroutine[T]`-typed slot.
			bool is_awaited = p_expression == awaited_node || call->is_coroutine_handle_capture;
			FSDataType type = _gdtype_from_datatype(call->get_datatype(), codegen.script);
			FSCodeGenerator::Address result;
			if (p_root) {
				result = FSCodeGenerator::Address(FSCodeGenerator::Address::NIL);
			} else {
				result = codegen.add_temporary(type);
			}

			// Evaluate the argument expressions, then bind them to the callee positionally. A
			// canonicalized named call records the source (written) evaluation order so its
			// side-effectful arguments run left to right as written even though `arguments` is in
			// parameter order; an ordinary call leaves it empty and evaluates front to back.
			Vector<FSCodeGenerator::Address> arguments;
			arguments.resize(call->arguments.size());
			const Vector<int> &evaluation_order = call->argument_evaluation_order;
			const bool has_evaluation_order = !evaluation_order.is_empty();
			int argument_temporaries_to_pop = 0;
			for (int order = 0; order < call->arguments.size(); order++) {
				const int i = has_evaluation_order ? evaluation_order[order] : order;
				FSCodeGenerator::Address arg = _parse_expression(codegen, r_error, call->arguments[i]);
				if (r_error) {
					return FSCodeGenerator::Address();
				}
				if (arg.mode == FSCodeGenerator::Address::TEMPORARY) {
					argument_temporaries_to_pop++;
				}
				arguments.write[i] = arg;
			}

			const int checked_argument_count = MIN(arguments.size(), call->resolved_parameter_types.size());
			for (int i = 0; i < checked_argument_count; i++) {
				if (call->synthesized_argument_indices.has(i)) {
					continue;
				}
				const FSDataType parameter_type = _gdtype_from_datatype(call->resolved_parameter_types[i], codegen.script);
				if (parameter_type.is_type_handle) {
					FSCodeGenerator::Address checked_argument = codegen.add_temporary(parameter_type);
					argument_temporaries_to_pop++;
					gen->write_assign_with_conversion(checked_argument, arguments[i]);
					arguments.write[i] = checked_argument;
				} else if (_is_erased_container_call_to_typed_array(call->arguments[i], parameter_type)) {
					FSCodeGenerator::Address checked_argument = codegen.add_temporary(parameter_type);
					argument_temporaries_to_pop++;
					gen->write_assign_typed_array_convert(checked_argument, arguments[i]);
					arguments.write[i] = checked_argument;
				} else if (_is_erased_container_call_to_typed_dictionary(call->arguments[i], parameter_type)) {
					FSCodeGenerator::Address checked_argument = codegen.add_temporary(parameter_type);
					argument_temporaries_to_pop++;
					gen->write_assign_typed_dictionary_convert(checked_argument, arguments[i]);
					arguments.write[i] = checked_argument;
				}
			}

			if (call->is_proxy_construct) {
				// `create_proxy[T](handler)` lowers to `create_proxy_dynamic(T, handler)`. The
				// type argument T (a trait/abstract type) yields the script the runtime uses to
				// scan the proxied contract; the result is typed as T by the analyzer.
				const FSParser::DataType call_datatype = call->get_datatype();
				const bool forwards_class_type_parameter = call_datatype.is_type_parameter() &&
						call_datatype.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS;

				FSCodeGenerator::Address type_arg;
				if (forwards_class_type_parameter) {
					// T is the enclosing generic class's type parameter, reified onto this
					// instance at construction (e.g. `Mock[Greeter].new()` binds T = Greeter).
					// Materialize its bound script from the instance's reified type arguments.
					type_arg = codegen.add_temporary();
					gen->write_get_type_parameter(type_arg, call_datatype.type_parameter_index);
				} else {
					// T is statically resolved; compile the `[T]` type argument as a value to
					// obtain its script.
					const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(call->callee);
					type_arg = _parse_expression(codegen, r_error, subscript->index);
					if (r_error) {
						return FSCodeGenerator::Address();
					}
				}

				Vector<FSCodeGenerator::Address> proxy_arguments;
				proxy_arguments.push_back(type_arg);
				for (int i = 0; i < arguments.size(); i++) {
					proxy_arguments.push_back(arguments[i]);
				}
				gen->write_call_foundry_script_utility(result, SNAME("create_proxy_dynamic"), proxy_arguments);
				if (type_arg.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}
			} else if (!call->is_super && call->callee->type == FSParser::Node::IDENTIFIER && FSParser::get_builtin_type(call->function_name) < Variant::VARIANT_MAX) {
				gen->write_construct(result, FSParser::get_builtin_type(call->function_name), arguments);
			} else if (!call->is_super && call->callee->type == FSParser::Node::IDENTIFIER && Variant::has_utility_function(call->function_name)) {
				// Variant utility function.
				gen->write_call_utility(result, call->function_name, arguments);
			} else if (!call->is_super && call->callee->type == FSParser::Node::IDENTIFIER && FSUtilityFunctions::function_exists(call->function_name)) {
				// FoundryScript utility function.
				gen->write_call_foundry_script_utility(result, call->function_name, arguments);
			} else {
				// Regular function.
				const FSParser::ExpressionNode *callee = call->callee;

				if (call->is_super) {
					// Super call.
					gen->write_super_call(result, call->function_name, arguments);
				} else {
					if (callee->type == FSParser::Node::IDENTIFIER) {
						// Self function call.
						if (ClassDB::has_method(codegen.script->native->get_name(), call->function_name)) {
							// Native method, use faster path.
							FSCodeGenerator::Address self;
							self.mode = FSCodeGenerator::Address::SELF;
							MethodBind *method = ClassDB::get_method(codegen.script->native->get_name(), call->function_name);

							if (_can_use_validate_call(method, arguments)) {
								// Exact arguments, use validated call.
								gen->write_call_method_bind_validated(result, self, method, arguments);
							} else {
								// Not exact arguments, but still can use method bind call.
								gen->write_call_method_bind(result, self, method, arguments);
							}
						} else if (call->is_static || codegen.is_static || (codegen.function_node && codegen.function_node->is_static) || call->function_name == "new") {
							FSCodeGenerator::Address self;
							self.mode = FSCodeGenerator::Address::CLASS;
							if (is_awaited) {
								gen->write_call_async(result, self, call->function_name, arguments);
							} else {
								gen->write_call(result, self, call->function_name, arguments);
							}
						} else {
							if (is_awaited) {
								gen->write_call_self_async(result, call->function_name, arguments);
							} else {
								gen->write_call_self(result, call->function_name, arguments);
							}
						}
					} else if (callee->type == FSParser::Node::SUBSCRIPT) {
						const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(call->callee);

						if (subscript->is_attribute) {
							// Specialized generic construction: `Box[int].new(...)`. The callee base is
							// itself an index subscript (`Box[int]`) carrying reified type arguments, which
							// must be bound onto the new instance instead of going through a plain call.
							const FSParser::SubscriptNode *specialization = nullptr;
							if (!call->is_super && call->function_name == SNAME("new") && subscript->base != nullptr && subscript->base->type == FSParser::Node::SUBSCRIPT) {
								const FSParser::SubscriptNode *candidate = static_cast<const FSParser::SubscriptNode *>(subscript->base);
								// Only a specialized class meta-type (`Box[int]`) is a construction target. An indexed
								// instance value (e.g. `array[0]` whose element type is `Box[int]`) also carries type
								// arguments, but is not a class handle, so it must not be treated as one.
								const FSParser::DataType candidate_type = candidate->get_datatype();
								if (!candidate->is_attribute && candidate_type.is_meta_type && candidate_type.kind == FSParser::DataType::CLASS) {
									specialization = candidate;
								}
							}
							// Mechanism (b) for stored/aliased specialized class handles (#242): the construction target is
							// the expression yielding the class object, and the reified arguments come from its static
							// meta-type. For the direct `Box[int].new()` that is `Box` with `Box[int]`'s arguments; for an
							// aliased handle (`const IntBox = Box[int]; IntBox.new()`, `var h = Box[int]; h.new()`) it is the
							// handle expression itself, whose static type is the specialized meta-type. A handle widened to
							// `FoundryScript`/`Object`/... carries no type arguments and falls through to plain construction.
							const FSParser::ExpressionNode *specialized_base = nullptr;
							FSCodeGenerator::Address expected_base;
							Vector<FSDataType> specialized_type_arguments;
							FSParser::DataType specialized_static_type;
							if (specialization != nullptr) {
								const FSDataType specialization_type = _gdtype_from_datatype(specialization->get_datatype(), codegen.script);
								if (!specialization_type.type_arguments.is_empty()) {
									specialized_base = specialization->base;
									specialized_static_type = specialization->get_datatype();
									specialized_type_arguments = specialization_type.type_arguments;
								}
							} else if (!call->is_super && call->function_name == SNAME("new") && subscript->base != nullptr) {
								const FSParser::DataType base_static = subscript->base->get_datatype();
								if (base_static.is_set() && (base_static.is_meta_type || base_static.is_type_handle_annotation) &&
										(base_static.kind == FSParser::DataType::CLASS || base_static.kind == FSParser::DataType::SCRIPT) &&
										!base_static.type_arguments.is_empty()) {
									// The handle's own type may be weakly inferred (an untyped `var h = Box[int]`), which would
									// make `_gdtype_from_datatype(base_static)` discard the whole type. The reified arguments
									// themselves are hard explicit types, so convert them individually instead.
									Vector<FSDataType> reified_arguments;
									for (int i = 0; i < base_static.type_arguments.size(); i++) {
										reified_arguments.push_back(_gdtype_from_datatype(base_static.type_arguments[i], codegen.script));
									}
									specialized_base = subscript->base;
									specialized_static_type = base_static;
									specialized_type_arguments = reified_arguments;
								}
							}

							if (specialized_base != nullptr) {
								FoundryScript *expected_class = nullptr;
								if (specialized_static_type.kind == FSParser::DataType::CLASS && specialized_static_type.class_type != nullptr && main_script != nullptr) {
									if (parser->has_class(specialized_static_type.class_type)) {
										expected_class = main_script->find_class(specialized_static_type.class_type->fqcn);
									} else {
										Error err = OK;
										Ref<FoundryScript> script = FSCache::get_shallow_script(specialized_static_type.script_path, err, codegen.script != nullptr ? codegen.script->path : String());
										if (err == OK && script.is_valid()) {
											expected_class = script->find_class(specialized_static_type.class_type->fqcn);
										}
									}
								} else if (specialized_static_type.kind == FSParser::DataType::SCRIPT && specialized_static_type.script_type.is_valid()) {
									expected_class = Object::cast_to<FoundryScript>(specialized_static_type.script_type.ptr());
								}
								if (expected_class != nullptr) {
									expected_base = codegen.add_constant(Ref<FoundryScript>(expected_class));
								} else {
									specialized_base = nullptr;
								}
							}

							if (specialized_base != nullptr) {
								// A specialized handle folded into a constant (e.g. `const IntBox = Box[int]`) bakes the
								// analyzer's shallow, uncompiled class object; constructing from it would fail since the
								// class never finishes compiling. Re-resolve it to the live class compiled in this unit —
								// the same object the direct `Box[int].new()` form instantiates. A non-constant handle
								// (`var h = Box[int]`) instead evaluates to its live runtime value, and anything not found
								// in this unit (e.g. an already-compiled preloaded script) keeps its folded value.
								FSCodeGenerator::Address base;
								FoundryScript *folded_class = nullptr;
								if (specialized_base->is_constant && specialized_base->reduced_value.get_type() == Variant::OBJECT) {
									folded_class = Object::cast_to<FoundryScript>(specialized_base->reduced_value);
								}
								FoundryScript *live_class = nullptr;
								if (folded_class != nullptr && !folded_class->is_valid() && main_script != nullptr) {
									live_class = main_script->find_class(folded_class->get_fully_qualified_name());
								}
								if (live_class != nullptr) {
									base = codegen.add_constant(Ref<FoundryScript>(live_class));
								} else {
									base = _parse_expression(codegen, r_error, specialized_base);
									if (r_error) {
										return FSCodeGenerator::Address();
									}
								}
								gen->write_construct_specialized(result, base, expected_base, specialized_type_arguments, arguments);
								if (base.mode == FSCodeGenerator::Address::TEMPORARY) {
									gen->pop_temporary();
								}
							} else if (!call->is_super && subscript->base->type == FSParser::Node::IDENTIFIER && FSParser::get_builtin_type(static_cast<FSParser::IdentifierNode *>(subscript->base)->name) < Variant::VARIANT_MAX) {
								// May be static built-in method call.
								gen->write_call_builtin_type_static(result, FSParser::get_builtin_type(static_cast<FSParser::IdentifierNode *>(subscript->base)->name), subscript->attribute->name, arguments);
							} else if (!call->is_super && subscript->base->type == FSParser::Node::IDENTIFIER && call->function_name != SNAME("new") &&
									static_cast<FSParser::IdentifierNode *>(subscript->base)->source == FSParser::IdentifierNode::NATIVE_CLASS && !Engine::get_singleton()->has_singleton(static_cast<FSParser::IdentifierNode *>(subscript->base)->name)) {
								// It's a static native method call.
								StringName class_name = static_cast<FSParser::IdentifierNode *>(subscript->base)->name;
								MethodBind *method = ClassDB::get_method(class_name, subscript->attribute->name);
								if (_can_use_validate_call(method, arguments)) {
									// Exact arguments, use validated call.
									gen->write_call_native_static_validated(result, method, arguments);
								} else {
									// Not exact arguments, use regular static call
									gen->write_call_native_static(result, class_name, subscript->attribute->name, arguments);
								}
							} else {
								FSCodeGenerator::Address base = _parse_expression(codegen, r_error, subscript->base);
								if (r_error) {
									return FSCodeGenerator::Address();
								}
								if (is_awaited) {
									gen->write_call_async(result, base, call->function_name, arguments);
								} else if (base.type.kind != FSDataType::VARIANT && base.type.kind != FSDataType::BUILTIN) {
									// Native method, use faster path.
									StringName class_name;
									if (base.type.kind == FSDataType::NATIVE) {
										class_name = base.type.native_type;
									} else {
										class_name = base.type.native_type == StringName() ? base.type.script_type->get_instance_base_type() : base.type.native_type;
									}
									if (FSAnalyzer::class_exists(class_name) && ClassDB::has_method(class_name, call->function_name)) {
										MethodBind *method = ClassDB::get_method(class_name, call->function_name);
										if (_can_use_validate_call(method, arguments)) {
											// Exact arguments, use validated call.
											gen->write_call_method_bind_validated(result, base, method, arguments);
										} else {
											// Not exact arguments, but still can use method bind call.
											gen->write_call_method_bind(result, base, method, arguments);
										}
									} else {
										gen->write_call(result, base, call->function_name, arguments);
									}
								} else if (base.type.kind == FSDataType::BUILTIN) {
									gen->write_call_builtin_type(result, base, base.type.builtin_type, call->function_name, arguments);
								} else {
									gen->write_call(result, base, call->function_name, arguments);
								}
								if (base.mode == FSCodeGenerator::Address::TEMPORARY) {
									gen->pop_temporary();
								}
							}
						} else if (!call->function_name.is_empty()) {
							// A validated generic-method application: `name[TypeArgs](...)` (dispatched on
							// `self`) or `receiver.method[TypeArgs](...)` (dispatched on the receiver). Type
							// arguments are erased at runtime, so this compiles as an ordinary call. The
							// analyzer rejects a genuine call on an index before codegen.
							const FSParser::SubscriptNode *receiver_access = nullptr;
							if (subscript->base != nullptr && subscript->base->type == FSParser::Node::SUBSCRIPT) {
								const FSParser::SubscriptNode *candidate = static_cast<const FSParser::SubscriptNode *>(subscript->base);
								if (candidate->is_attribute) {
									receiver_access = candidate;
								}
							}
							if (receiver_access != nullptr) {
								// Dispatch on the receiver, mirroring an ordinary `receiver.method(...)` call.
								FSCodeGenerator::Address base = _parse_expression(codegen, r_error, receiver_access->base);
								if (r_error) {
									return FSCodeGenerator::Address();
								}
								if (is_awaited) {
									gen->write_call_async(result, base, call->function_name, arguments);
								} else if (base.type.kind != FSDataType::VARIANT && base.type.kind != FSDataType::BUILTIN) {
									// Native method, use faster path.
									StringName class_name;
									if (base.type.kind == FSDataType::NATIVE) {
										class_name = base.type.native_type;
									} else {
										class_name = base.type.native_type == StringName() ? base.type.script_type->get_instance_base_type() : base.type.native_type;
									}
									if (FSAnalyzer::class_exists(class_name) && ClassDB::has_method(class_name, call->function_name)) {
										MethodBind *method = ClassDB::get_method(class_name, call->function_name);
										if (_can_use_validate_call(method, arguments)) {
											gen->write_call_method_bind_validated(result, base, method, arguments);
										} else {
											gen->write_call_method_bind(result, base, method, arguments);
										}
									} else {
										gen->write_call(result, base, call->function_name, arguments);
									}
								} else if (base.type.kind == FSDataType::BUILTIN) {
									gen->write_call_builtin_type(result, base, base.type.builtin_type, call->function_name, arguments);
								} else {
									gen->write_call(result, base, call->function_name, arguments);
								}
								if (base.mode == FSCodeGenerator::Address::TEMPORARY) {
									gen->pop_temporary();
								}
							} else if (call->is_static || codegen.is_static || (codegen.function_node && codegen.function_node->is_static)) {
								FSCodeGenerator::Address self;
								self.mode = FSCodeGenerator::Address::CLASS;
								if (is_awaited) {
									gen->write_call_async(result, self, call->function_name, arguments);
								} else {
									gen->write_call(result, self, call->function_name, arguments);
								}
							} else if (is_awaited) {
								gen->write_call_self_async(result, call->function_name, arguments);
							} else {
								gen->write_call_self(result, call->function_name, arguments);
							}
						} else {
							_set_error("Cannot call something that isn't a function.", call->callee);
							r_error = ERR_COMPILATION_FAILED;
							return FSCodeGenerator::Address();
						}
					} else {
						_set_error("Compiler bug (please report): incorrect callee type in call node.", call->callee);
						r_error = ERR_COMPILATION_FAILED;
						return FSCodeGenerator::Address();
					}
				}
			}

			for (int i = 0; i < argument_temporaries_to_pop; i++) {
				gen->pop_temporary();
			}
			return result;
		} break;
		case FSParser::Node::GET_NODE: {
			const FSParser::GetNodeNode *get_node = static_cast<const FSParser::GetNodeNode *>(p_expression);

			Vector<FSCodeGenerator::Address> args;
			args.push_back(codegen.add_constant(NodePath(get_node->full_path)));

			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(get_node->get_datatype(), codegen.script));

			MethodBind *get_node_method = ClassDB::get_method("Node", "get_node");
			gen->write_call_method_bind_validated(result, FSCodeGenerator::Address(FSCodeGenerator::Address::SELF), get_node_method, args);

			return result;
		} break;
		case FSParser::Node::PRELOAD: {
			const FSParser::PreloadNode *preload = static_cast<const FSParser::PreloadNode *>(p_expression);

			// Add resource as constant.
			return codegen.add_constant(preload->resource);
		} break;
		case FSParser::Node::AWAIT: {
			const FSParser::AwaitNode *await = static_cast<const FSParser::AwaitNode *>(p_expression);

			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(p_expression->get_datatype(), codegen.script));
			FSParser::ExpressionNode *previous_awaited_node = awaited_node;
			awaited_node = await->to_await;
			FSCodeGenerator::Address argument = _parse_expression(codegen, r_error, await->to_await);
			awaited_node = previous_awaited_node;
			if (r_error) {
				return FSCodeGenerator::Address();
			}

			gen->write_await(result, argument);

			if (argument.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}

			return result;
		} break;
		// Indexing operator.
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);

			// A specialized generic class meta-type used as a value (`Box[int]`, e.g. stored in a `const`/
			// `var` handle) evaluates to the base class object; the `[int]` type arguments are compile-time
			// metadata carried by the static type and recovered at a `.new()` call site, not a runtime index.
			// Without this, codegen would try to evaluate the type index `int` as an expression and fail.
			if (!subscript->is_attribute) {
				const FSParser::DataType subscript_type = subscript->get_datatype();
				if (subscript_type.is_meta_type && subscript_type.kind == FSParser::DataType::CLASS &&
						!subscript_type.type_arguments.is_empty()) {
					FoundryScript *base_class = nullptr;
					if (subscript_type.class_type != nullptr && main_script != nullptr) {
						if (parser->has_class(subscript_type.class_type)) {
							base_class = main_script->find_class(subscript_type.class_type->fqcn);
						} else {
							Error err = OK;
							Ref<FoundryScript> script = FSCache::get_shallow_script(subscript_type.script_path, err,
									codegen.script != nullptr ? codegen.script->path : String());
							if (err == OK && script.is_valid()) {
								base_class = script->find_class(subscript_type.class_type->fqcn);
							}
						}
					}
					if (base_class != nullptr) {
						Vector<ContainerType> type_arguments;
						for (const FSParser::DataType &argument : subscript_type.type_arguments) {
							type_arguments.push_back(_gdtype_from_datatype(argument, codegen.script).to_container_type());
						}
						return codegen.add_constant(FSSpecializedClassHandle::create(Ref<FoundryScript>(base_class), type_arguments));
					}
					return _parse_expression(codegen, r_error, subscript->base);
				}
			}

			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(subscript->get_datatype(), codegen.script));

			FSCodeGenerator::Address base = _parse_expression(codegen, r_error, subscript->base);
			if (r_error) {
				return FSCodeGenerator::Address();
			}

			bool named = subscript->is_attribute;
			StringName name;
			FSCodeGenerator::Address index;
			if (subscript->is_attribute) {
				if (subscript->base->type == FSParser::Node::SELF && codegen.script) {
					FSParser::IdentifierNode *identifier = subscript->attribute;
					HashMap<StringName, FoundryScript::MemberInfo>::Iterator MI = codegen.script->member_indices.find(identifier->name);

#ifdef DEBUG_ENABLED
					if (MI && MI->value.getter == codegen.function_name) {
						String n = identifier->name;
						_set_error("Must use '" + n + "' instead of 'self." + n + "' in getter.", identifier);
						r_error = ERR_COMPILATION_FAILED;
						return FSCodeGenerator::Address();
					}
#endif

					if (MI && MI->value.getter == "") {
						// Remove result temp as we don't need it.
						gen->pop_temporary();
						// Faster than indexing self (as if no self. had been used).
						return FSCodeGenerator::Address(FSCodeGenerator::Address::MEMBER, MI->value.index, _gdtype_from_datatype(subscript->get_datatype(), codegen.script));
					}
				}

				name = subscript->attribute->name;
				named = true;
			} else {
				if (subscript->index->is_constant && subscript->index->reduced_value.get_type() == Variant::STRING_NAME) {
					// Also, somehow, named (speed up anyway).
					name = subscript->index->reduced_value;
					named = true;
				} else {
					// Regular indexing.
					index = _parse_expression(codegen, r_error, subscript->index);
					if (r_error) {
						return FSCodeGenerator::Address();
					}
				}
			}

			if (named) {
				gen->write_get_named(result, name, base);
			} else {
				gen->write_get(result, index, base);
			}

			if (index.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}
			if (base.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}

			return result;
		} break;
		case FSParser::Node::UNARY_OPERATOR: {
			const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_expression);

			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(unary->get_datatype(), codegen.script));

			FSCodeGenerator::Address operand = _parse_expression(codegen, r_error, unary->operand);
			if (r_error) {
				return FSCodeGenerator::Address();
			}

			gen->write_unary_operator(result, unary->variant_op, operand);

			if (operand.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}

			return result;
		}
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);

			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(binary->get_datatype(), codegen.script));

			switch (binary->operation) {
				case FSParser::BinaryOpNode::OP_LOGIC_AND: {
					// AND operator with early out on failure.
					FSCodeGenerator::Address left_operand = _parse_expression(codegen, r_error, binary->left_operand);
					gen->write_and_left_operand(left_operand);
					FSCodeGenerator::Address right_operand = _parse_expression(codegen, r_error, binary->right_operand);
					gen->write_and_right_operand(right_operand);

					gen->write_end_and(result);

					if (right_operand.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
					if (left_operand.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
				} break;
				case FSParser::BinaryOpNode::OP_LOGIC_OR: {
					// OR operator with early out on success.
					FSCodeGenerator::Address left_operand = _parse_expression(codegen, r_error, binary->left_operand);
					gen->write_or_left_operand(left_operand);
					FSCodeGenerator::Address right_operand = _parse_expression(codegen, r_error, binary->right_operand);
					gen->write_or_right_operand(right_operand);

					gen->write_end_or(result);

					if (right_operand.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
					if (left_operand.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
				} break;
				default: {
					FSCodeGenerator::Address left_operand = _parse_expression(codegen, r_error, binary->left_operand);
					FSCodeGenerator::Address right_operand = _parse_expression(codegen, r_error, binary->right_operand);

					gen->write_binary_operator(result, binary->variant_op, left_operand, right_operand);

					if (right_operand.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
					if (left_operand.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
				}
			}
			return result;
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			// x IF a ELSE y operator with early out on failure.
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(ternary->get_datatype(), codegen.script));

			gen->write_start_ternary(result);

			FSCodeGenerator::Address condition = _parse_expression(codegen, r_error, ternary->condition);
			if (r_error) {
				return FSCodeGenerator::Address();
			}
			gen->write_ternary_condition(condition);

			if (condition.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}

			FSCodeGenerator::Address true_expr = _parse_expression(codegen, r_error, ternary->true_expr);
			if (r_error) {
				return FSCodeGenerator::Address();
			}
			gen->write_ternary_true_expr(true_expr);
			if (true_expr.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}

			FSCodeGenerator::Address false_expr = _parse_expression(codegen, r_error, ternary->false_expr);
			if (r_error) {
				return FSCodeGenerator::Address();
			}
			gen->write_ternary_false_expr(false_expr);
			if (false_expr.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}

			gen->write_end_ternary();

			return result;
		} break;
		case FSParser::Node::TYPE_TEST: {
			const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_expression);
			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(type_test->get_datatype(), codegen.script));

			FSCodeGenerator::Address operand = _parse_expression(codegen, r_error, type_test->operand);
			const bool handles_type_annotation = type_test->test_datatype.is_type_handle_annotation;
			FSDataType test_type = _gdtype_from_datatype(type_test->test_datatype, codegen.script, handles_type_annotation);
			if (r_error) {
				return FSCodeGenerator::Address();
			}

			if (test_type.has_type()) {
				gen->write_type_test(result, operand, test_type);
			} else {
				gen->write_assign_true(result);
			}

			if (operand.mode == FSCodeGenerator::Address::TEMPORARY) {
				gen->pop_temporary();
			}

			return result;
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);

			if (assignment->assignee->type == FSParser::Node::SUBSCRIPT) {
				// SET (chained) MODE!
				const FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(assignment->assignee);
#ifdef DEBUG_ENABLED
				if (subscript->is_attribute && subscript->base->type == FSParser::Node::SELF && codegen.script) {
					HashMap<StringName, FoundryScript::MemberInfo>::Iterator MI = codegen.script->member_indices.find(subscript->attribute->name);
					if (MI && MI->value.setter == codegen.function_name) {
						String n = subscript->attribute->name;
						_set_error("Must use '" + n + "' instead of 'self." + n + "' in setter.", subscript);
						r_error = ERR_COMPILATION_FAILED;
						return FSCodeGenerator::Address();
					}
				}
#endif
				/* Find chain of sets */

				StringName assign_class_member_property;

				FSCodeGenerator::Address target_member_property;
				bool is_member_property = false;
				bool member_property_has_setter = false;
				bool member_property_is_in_setter = false;
				bool is_static = false;
				FSCodeGenerator::Address static_var_class;
				int static_var_index = 0;
				FSDataType static_var_data_type;
				StringName var_name;
				StringName member_property_setter_function;

				List<const FSParser::SubscriptNode *> chain;

				{
					// Create get/set chain.
					const FSParser::SubscriptNode *n = subscript;
					while (true) {
						chain.push_back(n);
						if (n->base->type != FSParser::Node::SUBSCRIPT) {
							// Check for a property.
							if (n->base->type == FSParser::Node::IDENTIFIER) {
								FSParser::IdentifierNode *identifier = static_cast<FSParser::IdentifierNode *>(n->base);
								var_name = identifier->name;
								if (_is_class_member_property(codegen, var_name)) {
									assign_class_member_property = var_name;
								} else if (!_is_local_or_parameter(codegen, var_name)) {
									if (codegen.script->member_indices.has(var_name)) {
										is_member_property = true;
										is_static = false;
										const FoundryScript::MemberInfo &minfo = codegen.script->member_indices[var_name];
										member_property_setter_function = minfo.setter;
										member_property_has_setter = member_property_setter_function != StringName();
										member_property_is_in_setter = member_property_has_setter && member_property_setter_function == codegen.function_name;
										target_member_property.mode = FSCodeGenerator::Address::MEMBER;
										target_member_property.address = minfo.index;
										target_member_property.type = minfo.data_type;
									} else {
										// Try static variables.
										FoundryScript *scr = codegen.script;
										while (scr) {
											if (scr->static_variables_indices.has(var_name)) {
												is_member_property = true;
												is_static = true;
												const FoundryScript::MemberInfo &minfo = scr->static_variables_indices[var_name];
												member_property_setter_function = minfo.setter;
												member_property_has_setter = member_property_setter_function != StringName();
												member_property_is_in_setter = member_property_has_setter && member_property_setter_function == codegen.function_name;
												static_var_class = codegen.add_constant(scr);
												static_var_index = minfo.index;
												static_var_data_type = minfo.data_type;
												break;
											}
											scr = scr->base.ptr();
										}
									}
								}
							}
							break;
						}
						n = static_cast<const FSParser::SubscriptNode *>(n->base);
					}
				}

				/* Chain of gets */

				// Get at (potential) root stack pos, so it can be returned.
				FSCodeGenerator::Address base = _parse_expression(codegen, r_error, chain.back()->get()->base);
				const bool base_known_type = base.type.has_type();
				const bool base_is_shared = Variant::is_type_shared(base.type.builtin_type);

				if (r_error) {
					return FSCodeGenerator::Address();
				}

				FSCodeGenerator::Address prev_base = base;

				// In case the base has a setter, don't use the address directly, as we want to call that setter.
				// So use a temp value instead and call the setter at the end.
				FSCodeGenerator::Address base_temp;
				if ((!base_known_type || !base_is_shared) && base.mode == FSCodeGenerator::Address::MEMBER && member_property_has_setter && !member_property_is_in_setter) {
					base_temp = codegen.add_temporary(base.type);
					gen->write_assign(base_temp, base);
					prev_base = base_temp;
				}

				struct ChainInfo {
					bool is_named = false;
					FSCodeGenerator::Address base;
					FSCodeGenerator::Address key;
					StringName name;
				};

				List<ChainInfo> set_chain;

				for (List<const FSParser::SubscriptNode *>::Element *E = chain.back(); E; E = E->prev()) {
					if (E == chain.front()) {
						// Skip the main subscript, since we'll assign to that.
						break;
					}
					const FSParser::SubscriptNode *subscript_elem = E->get();
					FSCodeGenerator::Address value = codegen.add_temporary(_gdtype_from_datatype(subscript_elem->get_datatype(), codegen.script));
					FSCodeGenerator::Address key;
					StringName name;

					if (subscript_elem->is_attribute) {
						name = subscript_elem->attribute->name;
						gen->write_get_named(value, name, prev_base);
					} else {
						key = _parse_expression(codegen, r_error, subscript_elem->index);
						if (r_error) {
							return FSCodeGenerator::Address();
						}
						gen->write_get(value, key, prev_base);
					}

					// Store base and key for setting it back later.
					set_chain.push_front({ subscript_elem->is_attribute, prev_base, key, name }); // Push to front to invert the list.
					prev_base = value;
				}

				// Get value to assign.
				FSCodeGenerator::Address assigned = _parse_expression(codegen, r_error, assignment->assigned_value);
				if (r_error) {
					return FSCodeGenerator::Address();
				}
				// Get the key if needed.
				FSCodeGenerator::Address key;
				StringName name;
				if (subscript->is_attribute) {
					name = subscript->attribute->name;
				} else {
					key = _parse_expression(codegen, r_error, subscript->index);
					if (r_error) {
						return FSCodeGenerator::Address();
					}
				}

				// Perform operator if any.
				if (assignment->operation != FSParser::AssignmentNode::OP_NONE) {
					FSCodeGenerator::Address op_result = codegen.add_temporary(_gdtype_from_datatype(assignment->get_datatype(), codegen.script));
					FSCodeGenerator::Address value = codegen.add_temporary(_gdtype_from_datatype(subscript->get_datatype(), codegen.script));
					if (subscript->is_attribute) {
						gen->write_get_named(value, name, prev_base);
					} else {
						gen->write_get(value, key, prev_base);
					}
					gen->write_binary_operator(op_result, assignment->variant_op, value, assigned);
					gen->pop_temporary();
					if (assigned.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
					assigned = op_result;
				}

				FSCodeGenerator::Address value_to_set = assigned;
				bool converted_value_to_set = false;
				if (assignment->operation == FSParser::AssignmentNode::OP_NONE) {
					const FSDataType target_type = _gdtype_from_datatype(subscript->get_datatype(), codegen.script);
					if (_is_erased_container_call_to_typed_array(assignment->assigned_value, target_type)) {
						value_to_set = codegen.add_temporary(target_type);
						gen->write_assign_typed_array_convert(value_to_set, assigned);
						converted_value_to_set = true;
					} else if (_is_erased_container_call_to_typed_dictionary(assignment->assigned_value, target_type)) {
						value_to_set = codegen.add_temporary(target_type);
						gen->write_assign_typed_dictionary_convert(value_to_set, assigned);
						converted_value_to_set = true;
					}
				}

				// Perform assignment.
				if (subscript->is_attribute) {
					gen->write_set_named(prev_base, name, value_to_set);
				} else {
					gen->write_set(prev_base, key, value_to_set);
				}
				if (key.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}
				if (converted_value_to_set) {
					gen->pop_temporary();
				}
				if (assigned.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}

				assigned = prev_base;

				// Set back the values into their bases.
				for (const ChainInfo &info : set_chain) {
					bool known_type = assigned.type.has_type();
					bool is_shared = Variant::is_type_shared(assigned.type.builtin_type);

					if (!known_type || !is_shared) {
						if (!known_type) {
							// Jump shared values since they are already updated in-place.
							gen->write_jump_if_shared(assigned);
						}
						if (!info.is_named) {
							gen->write_set(info.base, info.key, assigned);
						} else {
							gen->write_set_named(info.base, info.name, assigned);
						}
						if (!known_type) {
							gen->write_end_jump_if_shared();
						}
					}
					if (!info.is_named && info.key.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
					if (assigned.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
					assigned = info.base;
				}

				bool known_type = assigned.type.has_type();
				bool is_shared = Variant::is_type_shared(assigned.type.builtin_type);

				if (!known_type || !is_shared) {
					// If this is a class member property, also assign to it.
					// This allow things like: position.x += 2.0
					if (assign_class_member_property != StringName()) {
						if (!known_type) {
							gen->write_jump_if_shared(assigned);
						}
						gen->write_set_member(assigned, assign_class_member_property);
						if (!known_type) {
							gen->write_end_jump_if_shared();
						}
					} else if (is_member_property) {
						// Same as above but for script members.
						if (!known_type) {
							gen->write_jump_if_shared(assigned);
						}
						if (member_property_has_setter && !member_property_is_in_setter) {
							Vector<FSCodeGenerator::Address> args;
							args.push_back(assigned);
							FSCodeGenerator::Address call_base = is_static ? FSCodeGenerator::Address(FSCodeGenerator::Address::CLASS) : FSCodeGenerator::Address(FSCodeGenerator::Address::SELF);
							gen->write_call(FSCodeGenerator::Address(), call_base, member_property_setter_function, args);
						} else if (is_static) {
							FSCodeGenerator::Address temp = codegen.add_temporary(static_var_data_type);
							gen->write_assign(temp, assigned);
							gen->write_set_static_variable(temp, static_var_class, static_var_index);
							gen->pop_temporary();
						} else {
							gen->write_assign(target_member_property, assigned);
						}
						if (!known_type) {
							gen->write_end_jump_if_shared();
						}
					}
				} else if (base_temp.mode == FSCodeGenerator::Address::TEMPORARY) {
					if (!base_known_type) {
						gen->write_jump_if_shared(base);
					}
					// Save the temp value back to the base by calling its setter.
					gen->write_call(FSCodeGenerator::Address(), base, member_property_setter_function, { assigned });
					if (!base_known_type) {
						gen->write_end_jump_if_shared();
					}
				}

				if (assigned.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}
			} else if (assignment->assignee->type == FSParser::Node::IDENTIFIER && _is_class_member_property(codegen, static_cast<FSParser::IdentifierNode *>(assignment->assignee)->name)) {
				// Assignment to member property.
				FSCodeGenerator::Address assigned_value = _parse_expression(codegen, r_error, assignment->assigned_value);
				if (r_error) {
					return FSCodeGenerator::Address();
				}

				FSCodeGenerator::Address to_assign = assigned_value;
				bool has_operation = assignment->operation != FSParser::AssignmentNode::OP_NONE;

				StringName name = static_cast<FSParser::IdentifierNode *>(assignment->assignee)->name;

				if (has_operation) {
					FSCodeGenerator::Address op_result = codegen.add_temporary(_gdtype_from_datatype(assignment->get_datatype(), codegen.script));
					FSCodeGenerator::Address member = codegen.add_temporary(_gdtype_from_datatype(assignment->assignee->get_datatype(), codegen.script));
					gen->write_get_member(member, name);
					gen->write_binary_operator(op_result, assignment->variant_op, member, assigned_value);
					gen->pop_temporary(); // Pop member temp.
					to_assign = op_result;
				}

				gen->write_set_member(to_assign, name);

				if (to_assign.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary(); // Pop the assigned expression or the temp result if it has operation.
				}
				if (has_operation && assigned_value.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary(); // Pop the assigned expression if not done before.
				}
			} else {
				// Regular assignment.
				if (assignment->assignee->type != FSParser::Node::IDENTIFIER) {
					_set_error("Compiler bug (please report): Expected the assignee to be an identifier here.", assignment->assignee);
					r_error = ERR_COMPILATION_FAILED;
					return FSCodeGenerator::Address();
				}
				FSCodeGenerator::Address member;
				bool is_member = false;
				bool has_setter = false;
				bool is_in_setter = false;
				bool is_static = false;
				int member_type_parameter_slot = -1;
				FSCodeGenerator::Address static_var_class;
				int static_var_index = 0;
				FSDataType static_var_data_type;
				StringName var_name;
				StringName setter_function;
				var_name = static_cast<const FSParser::IdentifierNode *>(assignment->assignee)->name;
				if (!_is_local_or_parameter(codegen, var_name)) {
					if (codegen.script->member_indices.has(var_name)) {
						is_member = true;
						is_static = false;
						FoundryScript::MemberInfo &minfo = codegen.script->member_indices[var_name];
						setter_function = minfo.setter;
						has_setter = setter_function != StringName();
						is_in_setter = has_setter && setter_function == codegen.function_name;
						member.mode = FSCodeGenerator::Address::MEMBER;
						member.address = minfo.index;
						member.type = minfo.data_type;
						if (minfo.type_argument_binding.kind != FoundryScript::TypeArgumentBinding::NONE) {
							member_type_parameter_slot = minfo.index;
						}
					} else {
						// Try static variables.
						FoundryScript *scr = codegen.script;
						while (scr) {
							if (scr->static_variables_indices.has(var_name)) {
								is_member = true;
								is_static = true;
								FoundryScript::MemberInfo &minfo = scr->static_variables_indices[var_name];
								setter_function = minfo.setter;
								has_setter = setter_function != StringName();
								is_in_setter = has_setter && setter_function == codegen.function_name;
								static_var_class = codegen.add_constant(scr);
								static_var_index = minfo.index;
								static_var_data_type = minfo.data_type;
								break;
							}
							scr = scr->base.ptr();
						}
					}
				}

				FSCodeGenerator::Address target;
				if (is_member) {
					target = member; // _parse_expression could call its getter, but we want to know the actual address
				} else {
					target = _parse_expression(codegen, r_error, assignment->assignee);
					if (r_error) {
						return FSCodeGenerator::Address();
					}
				}

				FSCodeGenerator::Address assigned_value = _parse_expression(codegen, r_error, assignment->assigned_value);
				if (r_error) {
					return FSCodeGenerator::Address();
				}

				FSCodeGenerator::Address to_assign;
				bool has_operation = assignment->operation != FSParser::AssignmentNode::OP_NONE;
				if (has_operation) {
					// Perform operation.
					FSCodeGenerator::Address op_result = codegen.add_temporary(_gdtype_from_datatype(assignment->get_datatype(), codegen.script));
					FSCodeGenerator::Address og_value = _parse_expression(codegen, r_error, assignment->assignee);
					gen->write_binary_operator(op_result, assignment->variant_op, og_value, assigned_value);
					to_assign = op_result;

					if (og_value.mode == FSCodeGenerator::Address::TEMPORARY) {
						gen->pop_temporary();
					}
				} else {
					to_assign = assigned_value;
				}

				if (has_setter && !is_in_setter) {
					// Call setter.
					FSCodeGenerator::Address setter_argument = to_assign;
					bool converted_setter_argument = false;
					if (!has_operation) {
						if (_is_erased_container_call_to_typed_array(assignment->assigned_value, target.type)) {
							setter_argument = codegen.add_temporary(target.type);
							gen->write_assign_typed_array_convert(setter_argument, to_assign);
							converted_setter_argument = true;
						} else if (_is_erased_container_call_to_typed_dictionary(assignment->assigned_value, target.type)) {
							setter_argument = codegen.add_temporary(target.type);
							gen->write_assign_typed_dictionary_convert(setter_argument, to_assign);
							converted_setter_argument = true;
						}
					}
					Vector<FSCodeGenerator::Address> args;
					args.push_back(setter_argument);
					FSCodeGenerator::Address call_base = is_static ? FSCodeGenerator::Address(FSCodeGenerator::Address::CLASS) : FSCodeGenerator::Address(FSCodeGenerator::Address::SELF);
					gen->write_call(FSCodeGenerator::Address(), call_base, setter_function, args);
					if (converted_setter_argument) {
						gen->pop_temporary();
					}
				} else if (is_static) {
					FSCodeGenerator::Address temp = codegen.add_temporary(static_var_data_type);
					if (assignment->use_conversion_assign) {
						gen->write_assign_with_conversion(temp, to_assign);
					} else {
						gen->write_assign(temp, to_assign);
					}
					gen->write_set_static_variable(temp, static_var_class, static_var_index);
					gen->pop_temporary();
				} else if (member_type_parameter_slot >= 0) {
					// Direct store into a `T`-typed member bypasses the setter/`set()` validation, so emit a
					// store that validates the value against the binding the leaf script resolved for this
					// member slot (fixed by an `extends Base[int]` specialization, or open on the instance).
					gen->write_assign_typed_parameter(target, to_assign, member_type_parameter_slot);
				} else if (!has_operation && _is_erased_container_call_to_typed_array(assignment->assigned_value, target.type)) {
					// The whole assigned value is a generic method returning an erased `Array[T]`; retype the
					// untyped runtime array into the concrete typed-array target.
					gen->write_assign_typed_array_convert(target, to_assign);
				} else if (!has_operation && _is_erased_container_call_to_typed_dictionary(assignment->assigned_value, target.type)) {
					// The whole assigned value is a generic method returning an erased `Dictionary[K, V]`; retype
					// the untyped runtime dictionary into the concrete typed-dictionary target.
					gen->write_assign_typed_dictionary_convert(target, to_assign);
				} else {
					// Just assign.
					if (assignment->use_conversion_assign) {
						gen->write_assign_with_conversion(target, to_assign);
					} else {
						gen->write_assign(target, to_assign);
					}
				}

				if (to_assign.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary(); // Pop assigned value or temp operation result.
				}
				if (has_operation && assigned_value.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary(); // Pop assigned value if not done before.
				}
				if (target.mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary(); // Pop the target to assignment.
				}
			}
			return FSCodeGenerator::Address(); // Assignment does not return a value.
		} break;
		case FSParser::Node::LAMBDA: {
			const FSParser::LambdaNode *lambda = static_cast<const FSParser::LambdaNode *>(p_expression);
			FSCodeGenerator::Address result = codegen.add_temporary(_gdtype_from_datatype(lambda->get_datatype(), codegen.script));

			Vector<FSCodeGenerator::Address> captures;
			captures.resize(lambda->captures.size());
			for (int i = 0; i < lambda->captures.size(); i++) {
				captures.write[i] = _parse_expression(codegen, r_error, lambda->captures[i]);
				if (r_error) {
					return FSCodeGenerator::Address();
				}
			}

			FSFunction *function = _parse_function(r_error, codegen.script, codegen.class_node, lambda->function, false, true);
			if (r_error) {
				return FSCodeGenerator::Address();
			}

			codegen.script->lambda_info.insert(function, { (int)lambda->captures.size(), lambda->use_self });
			gen->write_lambda(result, function, captures, lambda->use_self);

			for (int i = 0; i < captures.size(); i++) {
				if (captures[i].mode == FSCodeGenerator::Address::TEMPORARY) {
					gen->pop_temporary();
				}
			}

			return result;
		} break;
		default: {
			_set_error("Compiler bug (please report): Unexpected node in parse tree while parsing expression.", p_expression); // Unreachable code.
			r_error = ERR_COMPILATION_FAILED;
			return FSCodeGenerator::Address();
		} break;
	}
}

FSCodeGenerator::Address FSCompiler::_parse_match_pattern(CodeGen &codegen, Error &r_error, const FSParser::PatternNode *p_pattern, const FSCodeGenerator::Address &p_value_addr, const FSCodeGenerator::Address &p_type_addr, const FSCodeGenerator::Address &p_previous_test, bool p_is_first, bool p_is_nested) {
	switch (p_pattern->pattern_type) {
		case FSParser::PatternNode::PT_LITERAL: {
			if (p_is_nested) {
				codegen.generator->write_and_left_operand(p_previous_test);
			} else if (!p_is_first) {
				codegen.generator->write_or_left_operand(p_previous_test);
			}

			// Get literal type into constant map.
			Variant::Type literal_type = p_pattern->literal->value.get_type();
			FSCodeGenerator::Address literal_type_addr = codegen.add_constant(literal_type);

			// Equality is always a boolean.
			FSDataType equality_type;
			equality_type.kind = FSDataType::BUILTIN;
			equality_type.builtin_type = Variant::BOOL;

			// Check type equality.
			FSCodeGenerator::Address type_equality_addr = codegen.add_temporary(equality_type);
			codegen.generator->write_binary_operator(type_equality_addr, Variant::OP_EQUAL, p_type_addr, literal_type_addr);

			if (literal_type == Variant::STRING) {
				FSCodeGenerator::Address type_stringname_addr = codegen.add_constant(Variant::STRING_NAME);

				// Check StringName <-> String type equality.
				FSCodeGenerator::Address tmp_comp_addr = codegen.add_temporary(equality_type);

				codegen.generator->write_binary_operator(tmp_comp_addr, Variant::OP_EQUAL, p_type_addr, type_stringname_addr);
				codegen.generator->write_binary_operator(type_equality_addr, Variant::OP_OR, type_equality_addr, tmp_comp_addr);

				codegen.generator->pop_temporary(); // Remove tmp_comp_addr from stack.
			} else if (literal_type == Variant::STRING_NAME) {
				FSCodeGenerator::Address type_string_addr = codegen.add_constant(Variant::STRING);

				// Check String <-> StringName type equality.
				FSCodeGenerator::Address tmp_comp_addr = codegen.add_temporary(equality_type);

				codegen.generator->write_binary_operator(tmp_comp_addr, Variant::OP_EQUAL, p_type_addr, type_string_addr);
				codegen.generator->write_binary_operator(type_equality_addr, Variant::OP_OR, type_equality_addr, tmp_comp_addr);

				codegen.generator->pop_temporary(); // Remove tmp_comp_addr from stack.
			}

			codegen.generator->write_and_left_operand(type_equality_addr);

			// Get literal.
			FSCodeGenerator::Address literal_addr = _parse_expression(codegen, r_error, p_pattern->literal);
			if (r_error) {
				return FSCodeGenerator::Address();
			}

			// Check value equality.
			FSCodeGenerator::Address equality_addr = codegen.add_temporary(equality_type);
			codegen.generator->write_binary_operator(equality_addr, Variant::OP_EQUAL, p_value_addr, literal_addr);
			codegen.generator->write_and_right_operand(equality_addr);

			// AND both together (reuse temporary location).
			codegen.generator->write_end_and(type_equality_addr);

			codegen.generator->pop_temporary(); // Remove equality_addr from stack.

			if (literal_addr.mode == FSCodeGenerator::Address::TEMPORARY) {
				codegen.generator->pop_temporary();
			}

			// If this isn't the first, we need to OR with the previous pattern. If it's nested, we use AND instead.
			if (p_is_nested) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_and_right_operand(type_equality_addr);
				codegen.generator->write_end_and(p_previous_test);
			} else if (!p_is_first) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_or_right_operand(type_equality_addr);
				codegen.generator->write_end_or(p_previous_test);
			} else {
				// Just assign this value to the accumulator temporary.
				codegen.generator->write_assign(p_previous_test, type_equality_addr);
			}
			codegen.generator->pop_temporary(); // Remove type_equality_addr.

			return p_previous_test;
		} break;
		case FSParser::PatternNode::PT_EXPRESSION: {
			if (p_is_nested) {
				codegen.generator->write_and_left_operand(p_previous_test);
			} else if (!p_is_first) {
				codegen.generator->write_or_left_operand(p_previous_test);
			}

			FSCodeGenerator::Address type_string_addr = codegen.add_constant(Variant::STRING);
			FSCodeGenerator::Address type_stringname_addr = codegen.add_constant(Variant::STRING_NAME);

			// Equality is always a boolean.
			FSDataType equality_type;
			equality_type.kind = FSDataType::BUILTIN;
			equality_type.builtin_type = Variant::BOOL;

			// Create the result temps first since it's the last to go away.
			FSCodeGenerator::Address result_addr = codegen.add_temporary(equality_type);
			FSCodeGenerator::Address equality_test_addr = codegen.add_temporary(equality_type);
			FSCodeGenerator::Address stringy_comp_addr = codegen.add_temporary(equality_type);
			FSCodeGenerator::Address stringy_comp_addr_2 = codegen.add_temporary(equality_type);
			FSCodeGenerator::Address expr_type_addr = codegen.add_temporary();

			// Evaluate expression.
			FSCodeGenerator::Address expr_addr;
			expr_addr = _parse_expression(codegen, r_error, p_pattern->expression);
			if (r_error) {
				return FSCodeGenerator::Address();
			}

			// Evaluate expression type.
			Vector<FSCodeGenerator::Address> typeof_args;
			typeof_args.push_back(expr_addr);
			codegen.generator->write_call_utility(expr_type_addr, "typeof", typeof_args);

			// Check type equality.
			codegen.generator->write_binary_operator(result_addr, Variant::OP_EQUAL, p_type_addr, expr_type_addr);

			// Check for String <-> StringName comparison.
			codegen.generator->write_binary_operator(stringy_comp_addr, Variant::OP_EQUAL, p_type_addr, type_string_addr);
			codegen.generator->write_binary_operator(stringy_comp_addr_2, Variant::OP_EQUAL, expr_type_addr, type_stringname_addr);
			codegen.generator->write_binary_operator(stringy_comp_addr, Variant::OP_AND, stringy_comp_addr, stringy_comp_addr_2);
			codegen.generator->write_binary_operator(result_addr, Variant::OP_OR, result_addr, stringy_comp_addr);

			// Check for StringName <-> String comparison.
			codegen.generator->write_binary_operator(stringy_comp_addr, Variant::OP_EQUAL, p_type_addr, type_stringname_addr);
			codegen.generator->write_binary_operator(stringy_comp_addr_2, Variant::OP_EQUAL, expr_type_addr, type_string_addr);
			codegen.generator->write_binary_operator(stringy_comp_addr, Variant::OP_AND, stringy_comp_addr, stringy_comp_addr_2);
			codegen.generator->write_binary_operator(result_addr, Variant::OP_OR, result_addr, stringy_comp_addr);

			codegen.generator->pop_temporary(); // Remove expr_type_addr from stack.
			codegen.generator->pop_temporary(); // Remove stringy_comp_addr_2 from stack.
			codegen.generator->pop_temporary(); // Remove stringy_comp_addr from stack.

			codegen.generator->write_and_left_operand(result_addr);

			// Check value equality.
			codegen.generator->write_binary_operator(equality_test_addr, Variant::OP_EQUAL, p_value_addr, expr_addr);
			codegen.generator->write_and_right_operand(equality_test_addr);

			// AND both type and value equality.
			codegen.generator->write_end_and(result_addr);

			// We don't need the expression temporary anymore.
			if (expr_addr.mode == FSCodeGenerator::Address::TEMPORARY) {
				codegen.generator->pop_temporary();
			}
			codegen.generator->pop_temporary(); // Remove equality_test_addr from stack.

			// If this isn't the first, we need to OR with the previous pattern. If it's nested, we use AND instead.
			if (p_is_nested) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_and_right_operand(result_addr);
				codegen.generator->write_end_and(p_previous_test);
			} else if (!p_is_first) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_or_right_operand(result_addr);
				codegen.generator->write_end_or(p_previous_test);
			} else {
				// Just assign this value to the accumulator temporary.
				codegen.generator->write_assign(p_previous_test, result_addr);
			}
			codegen.generator->pop_temporary(); // Remove temp result addr.

			return p_previous_test;
		} break;
		case FSParser::PatternNode::PT_ARRAY: {
			if (p_is_nested) {
				codegen.generator->write_and_left_operand(p_previous_test);
			} else if (!p_is_first) {
				codegen.generator->write_or_left_operand(p_previous_test);
			}
			// Get array type into constant map.
			FSCodeGenerator::Address array_type_addr = codegen.add_constant((int)Variant::ARRAY);

			// Equality is always a boolean.
			FSDataType temp_type;
			temp_type.kind = FSDataType::BUILTIN;
			temp_type.builtin_type = Variant::BOOL;

			// Check type equality.
			FSCodeGenerator::Address result_addr = codegen.add_temporary(temp_type);
			codegen.generator->write_binary_operator(result_addr, Variant::OP_EQUAL, p_type_addr, array_type_addr);
			codegen.generator->write_and_left_operand(result_addr);

			// Store pattern length in constant map.
			FSCodeGenerator::Address array_length_addr = codegen.add_constant(p_pattern->rest_used ? p_pattern->array.size() - 1 : p_pattern->array.size());

			// Get value length.
			temp_type.builtin_type = Variant::INT;
			FSCodeGenerator::Address value_length_addr = codegen.add_temporary(temp_type);
			Vector<FSCodeGenerator::Address> len_args;
			len_args.push_back(p_value_addr);
			codegen.generator->write_call_foundry_script_utility(value_length_addr, "len", len_args);

			// Test length compatibility.
			temp_type.builtin_type = Variant::BOOL;
			FSCodeGenerator::Address length_compat_addr = codegen.add_temporary(temp_type);
			codegen.generator->write_binary_operator(length_compat_addr, p_pattern->rest_used ? Variant::OP_GREATER_EQUAL : Variant::OP_EQUAL, value_length_addr, array_length_addr);
			codegen.generator->write_and_right_operand(length_compat_addr);

			// AND type and length check.
			codegen.generator->write_end_and(result_addr);

			// Remove length temporaries.
			codegen.generator->pop_temporary();
			codegen.generator->pop_temporary();

			// Create temporaries outside the loop so they can be reused.
			FSCodeGenerator::Address element_addr = codegen.add_temporary();
			FSCodeGenerator::Address element_type_addr = codegen.add_temporary();

			// Evaluate element by element.
			for (int i = 0; i < p_pattern->array.size(); i++) {
				if (p_pattern->array[i]->pattern_type == FSParser::PatternNode::PT_REST) {
					// Don't want to access an extra element of the user array.
					break;
				}

				// Use AND here too, as we don't want to be checking elements if previous test failed (which means this might be an invalid get).
				codegen.generator->write_and_left_operand(result_addr);

				// Add index to constant map.
				FSCodeGenerator::Address index_addr = codegen.add_constant(i);

				// Get the actual element from the user-sent array.
				codegen.generator->write_get(element_addr, index_addr, p_value_addr);

				// Also get type of element.
				Vector<FSCodeGenerator::Address> typeof_args;
				typeof_args.push_back(element_addr);
				codegen.generator->write_call_utility(element_type_addr, "typeof", typeof_args);

				// Try the pattern inside the element.
				result_addr = _parse_match_pattern(codegen, r_error, p_pattern->array[i], element_addr, element_type_addr, result_addr, false, true);
				if (r_error != OK) {
					return FSCodeGenerator::Address();
				}

				codegen.generator->write_and_right_operand(result_addr);
				codegen.generator->write_end_and(result_addr);
			}
			// Remove element temporaries.
			codegen.generator->pop_temporary();
			codegen.generator->pop_temporary();

			// If this isn't the first, we need to OR with the previous pattern. If it's nested, we use AND instead.
			if (p_is_nested) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_and_right_operand(result_addr);
				codegen.generator->write_end_and(p_previous_test);
			} else if (!p_is_first) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_or_right_operand(result_addr);
				codegen.generator->write_end_or(p_previous_test);
			} else {
				// Just assign this value to the accumulator temporary.
				codegen.generator->write_assign(p_previous_test, result_addr);
			}
			codegen.generator->pop_temporary(); // Remove temp result addr.

			return p_previous_test;
		} break;
		case FSParser::PatternNode::PT_DICTIONARY: {
			if (p_is_nested) {
				codegen.generator->write_and_left_operand(p_previous_test);
			} else if (!p_is_first) {
				codegen.generator->write_or_left_operand(p_previous_test);
			}
			// Get dictionary type into constant map.
			FSCodeGenerator::Address dict_type_addr = codegen.add_constant((int)Variant::DICTIONARY);

			// Equality is always a boolean.
			FSDataType temp_type;
			temp_type.kind = FSDataType::BUILTIN;
			temp_type.builtin_type = Variant::BOOL;

			// Check type equality.
			FSCodeGenerator::Address result_addr = codegen.add_temporary(temp_type);
			codegen.generator->write_binary_operator(result_addr, Variant::OP_EQUAL, p_type_addr, dict_type_addr);
			codegen.generator->write_and_left_operand(result_addr);

			// Store pattern length in constant map.
			FSCodeGenerator::Address dict_length_addr = codegen.add_constant(p_pattern->rest_used ? p_pattern->dictionary.size() - 1 : p_pattern->dictionary.size());

			// Get user's dictionary length.
			temp_type.builtin_type = Variant::INT;
			FSCodeGenerator::Address value_length_addr = codegen.add_temporary(temp_type);
			Vector<FSCodeGenerator::Address> func_args;
			func_args.push_back(p_value_addr);
			codegen.generator->write_call_foundry_script_utility(value_length_addr, "len", func_args);

			// Test length compatibility.
			temp_type.builtin_type = Variant::BOOL;
			FSCodeGenerator::Address length_compat_addr = codegen.add_temporary(temp_type);
			codegen.generator->write_binary_operator(length_compat_addr, p_pattern->rest_used ? Variant::OP_GREATER_EQUAL : Variant::OP_EQUAL, value_length_addr, dict_length_addr);
			codegen.generator->write_and_right_operand(length_compat_addr);

			// AND type and length check.
			codegen.generator->write_end_and(result_addr);

			// Remove length temporaries.
			codegen.generator->pop_temporary();
			codegen.generator->pop_temporary();

			// Create temporaries outside the loop so they can be reused.
			FSCodeGenerator::Address element_addr = codegen.add_temporary();
			FSCodeGenerator::Address element_type_addr = codegen.add_temporary();

			// Evaluate element by element.
			for (int i = 0; i < p_pattern->dictionary.size(); i++) {
				const FSParser::PatternNode::Pair &element = p_pattern->dictionary[i];
				if (element.value_pattern && element.value_pattern->pattern_type == FSParser::PatternNode::PT_REST) {
					// Ignore rest pattern.
					break;
				}

				// Use AND here too, as we don't want to be checking elements if previous test failed (which means this might be an invalid get).
				codegen.generator->write_and_left_operand(result_addr);

				// Get the pattern key.
				FSCodeGenerator::Address pattern_key_addr = _parse_expression(codegen, r_error, element.key);
				if (r_error) {
					return FSCodeGenerator::Address();
				}

				// Check if pattern key exists in user's dictionary. This will be AND-ed with next result.
				func_args.clear();
				func_args.push_back(pattern_key_addr);
				codegen.generator->write_call(result_addr, p_value_addr, "has", func_args);

				if (element.value_pattern != nullptr) {
					// Use AND here too, as we don't want to be checking elements if previous test failed (which means this might be an invalid get).
					codegen.generator->write_and_left_operand(result_addr);

					// Get actual value from user dictionary.
					codegen.generator->write_get(element_addr, pattern_key_addr, p_value_addr);

					// Also get type of value.
					func_args.clear();
					func_args.push_back(element_addr);
					codegen.generator->write_call_utility(element_type_addr, "typeof", func_args);

					// Try the pattern inside the value.
					result_addr = _parse_match_pattern(codegen, r_error, element.value_pattern, element_addr, element_type_addr, result_addr, false, true);
					if (r_error != OK) {
						return FSCodeGenerator::Address();
					}
					codegen.generator->write_and_right_operand(result_addr);
					codegen.generator->write_end_and(result_addr);
				}

				codegen.generator->write_and_right_operand(result_addr);
				codegen.generator->write_end_and(result_addr);

				// Remove pattern key temporary.
				if (pattern_key_addr.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}
			}

			// Remove element temporaries.
			codegen.generator->pop_temporary();
			codegen.generator->pop_temporary();

			// If this isn't the first, we need to OR with the previous pattern. If it's nested, we use AND instead.
			if (p_is_nested) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_and_right_operand(result_addr);
				codegen.generator->write_end_and(p_previous_test);
			} else if (!p_is_first) {
				// Use the previous value as target, since we only need one temporary variable.
				codegen.generator->write_or_right_operand(result_addr);
				codegen.generator->write_end_or(p_previous_test);
			} else {
				// Just assign this value to the accumulator temporary.
				codegen.generator->write_assign(p_previous_test, result_addr);
			}
			codegen.generator->pop_temporary(); // Remove temp result addr.

			return p_previous_test;
		} break;
		case FSParser::PatternNode::PT_REST:
			// Do nothing.
			return p_previous_test;
			break;
		case FSParser::PatternNode::PT_BIND: {
			if (p_is_nested) {
				codegen.generator->write_and_left_operand(p_previous_test);
			} else if (!p_is_first) {
				codegen.generator->write_or_left_operand(p_previous_test);
			}
			// Get the bind address.
			FSCodeGenerator::Address bind = codegen.locals[p_pattern->bind->name];

			// Assign value to bound variable.
			codegen.generator->write_assign(bind, p_value_addr);
		}
			[[fallthrough]]; // Act like matching anything too.
		case FSParser::PatternNode::PT_WILDCARD:
			// If this is a fall through we don't want to do this again.
			if (p_pattern->pattern_type != FSParser::PatternNode::PT_BIND) {
				if (p_is_nested) {
					codegen.generator->write_and_left_operand(p_previous_test);
				} else if (!p_is_first) {
					codegen.generator->write_or_left_operand(p_previous_test);
				}
			}
			// This matches anything so just do the same as `if(true)`.
			// If this isn't the first, we need to OR with the previous pattern. If it's nested, we use AND instead.
			if (p_is_nested) {
				// Use the operator with the `true` constant so it works as always matching.
				FSCodeGenerator::Address constant = codegen.add_constant(true);
				codegen.generator->write_and_right_operand(constant);
				codegen.generator->write_end_and(p_previous_test);
			} else if (!p_is_first) {
				// Use the operator with the `true` constant so it works as always matching.
				FSCodeGenerator::Address constant = codegen.add_constant(true);
				codegen.generator->write_or_right_operand(constant);
				codegen.generator->write_end_or(p_previous_test);
			} else {
				// Just assign this value to the accumulator temporary.
				codegen.generator->write_assign_true(p_previous_test);
			}
			return p_previous_test;
	}

	_set_error("Compiler bug (please report): Reaching the end of pattern compilation without matching a pattern.", p_pattern);
	r_error = ERR_COMPILATION_FAILED;
	return p_previous_test;
}

List<FSCodeGenerator::Address> FSCompiler::_add_block_locals(CodeGen &codegen, const FSParser::SuiteNode *p_block) {
	List<FSCodeGenerator::Address> addresses;
	for (int i = 0; i < p_block->locals.size(); i++) {
		if (p_block->locals[i].type == FSParser::SuiteNode::Local::PARAMETER || p_block->locals[i].type == FSParser::SuiteNode::Local::FOR_VARIABLE) {
			// Parameters are added directly from function and loop variables are declared explicitly.
			continue;
		}
		addresses.push_back(codegen.add_local(p_block->locals[i].name, _gdtype_from_datatype(p_block->locals[i].get_datatype(), codegen.script)));
	}
	return addresses;
}

// Avoid keeping in the stack long-lived references to objects, which may prevent `RefCounted` objects from being freed.
void FSCompiler::_clear_block_locals(CodeGen &codegen, const List<FSCodeGenerator::Address> &p_locals) {
	for (const FSCodeGenerator::Address &local : p_locals) {
		if (local.type.can_contain_object()) {
			codegen.generator->clear_address(local);
		}
	}
}

Error FSCompiler::_parse_block(CodeGen &codegen, const FSParser::SuiteNode *p_block, bool p_add_locals, bool p_clear_locals) {
	Error err = OK;
	FSCodeGenerator *gen = codegen.generator;
	List<FSCodeGenerator::Address> block_locals;

	gen->clear_temporaries();
	codegen.start_block();

	if (p_add_locals) {
		block_locals = _add_block_locals(codegen, p_block);
	}

	for (int i = 0; i < p_block->statements.size(); i++) {
		const FSParser::Node *s = p_block->statements[i];

		gen->write_newline(s->start_line);

		switch (s->type) {
			case FSParser::Node::MATCH: {
				const FSParser::MatchNode *match = static_cast<const FSParser::MatchNode *>(s);

				codegen.start_block(); // Add an extra block, since @special locals belong to the match scope.

				// Evaluate the match expression.
				FSCodeGenerator::Address value = codegen.add_local("@match_value", _gdtype_from_datatype(match->test->get_datatype(), codegen.script));
				FSCodeGenerator::Address value_expr = _parse_expression(codegen, err, match->test);
				if (err) {
					return err;
				}

				// Assign to local.
				// TODO: This can be improved by passing the target to parse_expression().
				gen->write_assign(value, value_expr);

				if (value_expr.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}

				// Then, let's save the type of the value in the stack too, so we can reuse for later comparisons.
				FSDataType typeof_type;
				typeof_type.kind = FSDataType::BUILTIN;
				typeof_type.builtin_type = Variant::INT;
				FSCodeGenerator::Address type = codegen.add_local("@match_type", typeof_type);

				Vector<FSCodeGenerator::Address> typeof_args;
				typeof_args.push_back(value);
				gen->write_call_utility(type, "typeof", typeof_args);

				// Now we can actually start testing.
				// For each branch.
				for (int j = 0; j < match->branches.size(); j++) {
					if (j > 0) {
						// Use `else` to not check the next branch after matching.
						gen->write_else();
					}

					const FSParser::MatchBranchNode *branch = match->branches[j];

					codegen.start_block(); // Add an extra block, since binds belong to the match branch scope.

					// Add locals in block before patterns, so temporaries don't use the stack address for binds.
					List<FSCodeGenerator::Address> branch_locals = _add_block_locals(codegen, branch->block);

					gen->write_newline(branch->start_line);

					// For each pattern in branch.
					FSCodeGenerator::Address pattern_result = codegen.add_temporary();
					for (int k = 0; k < branch->patterns.size(); k++) {
						pattern_result = _parse_match_pattern(codegen, err, branch->patterns[k], value, type, pattern_result, k == 0, false);
						if (err != OK) {
							return err;
						}
					}

					// If there's a guard, check its condition too.
					if (branch->guard_body != nullptr) {
						// Do this first so the guard does not run unless the pattern matched.
						gen->write_and_left_operand(pattern_result);

						// Don't actually use the block for the guard.
						// The binds are already in the locals and we don't want to clear the result of the guard condition before we check the actual match.
						FSCodeGenerator::Address guard_result = _parse_expression(codegen, err, static_cast<FSParser::ExpressionNode *>(branch->guard_body->statements[0]));
						if (err) {
							return err;
						}

						gen->write_and_right_operand(guard_result);
						gen->write_end_and(pattern_result);

						if (guard_result.mode == FSCodeGenerator::Address::TEMPORARY) {
							codegen.generator->pop_temporary();
						}
					}

					// Check if pattern did match.
					gen->write_if(pattern_result);

					// Remove the result from stack.
					gen->pop_temporary();

					// Parse the branch block.
					err = _parse_block(codegen, branch->block, false); // Don't add locals again.
					if (err) {
						return err;
					}

					_clear_block_locals(codegen, branch_locals);

					codegen.end_block(); // Get out of extra block for binds.
				}

				// End all nested `if`s.
				for (int j = 0; j < match->branches.size(); j++) {
					gen->write_endif();
				}

				codegen.end_block(); // Get out of extra block for match's @special locals.
			} break;
			case FSParser::Node::IF: {
				const FSParser::IfNode *if_n = static_cast<const FSParser::IfNode *>(s);
				FSCodeGenerator::Address condition = _parse_expression(codegen, err, if_n->condition);
				if (err) {
					return err;
				}

				gen->write_if(condition);

				if (condition.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}

				err = _parse_block(codegen, if_n->true_block);
				if (err) {
					return err;
				}

				if (if_n->false_block) {
					gen->write_else();

					err = _parse_block(codegen, if_n->false_block);
					if (err) {
						return err;
					}
				}

				gen->write_endif();
			} break;
			case FSParser::Node::FOR: {
				const FSParser::ForNode *for_n = static_cast<const FSParser::ForNode *>(s);

				// Add an extra block, since the iterator and @special locals belong to the loop scope.
				// Also we use custom logic to clear block locals.
				codegen.start_block();

				FSCodeGenerator::Address iterator = codegen.add_local(for_n->variable->name, _gdtype_from_datatype(for_n->variable->get_datatype(), codegen.script));

				// Optimize `range()` call to not allocate an array.
				FSParser::CallNode *range_call = nullptr;
				if (for_n->list && for_n->list->type == FSParser::Node::CALL) {
					FSParser::CallNode *call = static_cast<FSParser::CallNode *>(for_n->list);
					if (call->get_callee_type() == FSParser::Node::IDENTIFIER) {
						if (static_cast<FSParser::IdentifierNode *>(call->callee)->name == "range") {
							range_call = call;
						}
					}
				}

				gen->start_for(iterator.type, _gdtype_from_datatype(for_n->list->get_datatype(), codegen.script), range_call != nullptr);

				if (range_call != nullptr) {
					Vector<FSCodeGenerator::Address> args;
					args.resize(range_call->arguments.size());

					for (int j = 0; j < args.size(); j++) {
						args.write[j] = _parse_expression(codegen, err, range_call->arguments[j]);
						if (err) {
							return err;
						}
					}

					switch (args.size()) {
						case 1:
							gen->write_for_range_assignment(codegen.add_constant(0), args[0], codegen.add_constant(1));
							break;
						case 2:
							gen->write_for_range_assignment(args[0], args[1], codegen.add_constant(1));
							break;
						case 3:
							gen->write_for_range_assignment(args[0], args[1], args[2]);
							break;
						default:
							_set_error(R"*(Analyzer bug: Wrong "range()" argument count.)*", range_call);
							return ERR_BUG;
					}

					for (int j = 0; j < args.size(); j++) {
						if (args[j].mode == FSCodeGenerator::Address::TEMPORARY) {
							codegen.generator->pop_temporary();
						}
					}
				} else {
					FSCodeGenerator::Address list = _parse_expression(codegen, err, for_n->list);
					if (err) {
						return err;
					}

					gen->write_for_list_assignment(list);

					if (list.mode == FSCodeGenerator::Address::TEMPORARY) {
						codegen.generator->pop_temporary();
					}
				}

				gen->write_for(iterator, for_n->use_conversion_assign, range_call != nullptr);

				// Loop variables must be cleared even when `break`/`continue` is used.
				List<FSCodeGenerator::Address> loop_locals = _add_block_locals(codegen, for_n->loop);

				_clear_block_locals(codegen, loop_locals); // Inside loop, before block - for `continue`.

				err = _parse_block(codegen, for_n->loop, false); // Don't add locals again.
				if (err) {
					return err;
				}

				gen->write_endfor(range_call != nullptr);

				_clear_block_locals(codegen, loop_locals); // Outside loop, after block - for `break` and normal exit.

				codegen.end_block(); // Get out of extra block for loop iterator, @special locals, and custom locals clearing.
			} break;
			case FSParser::Node::WHILE: {
				const FSParser::WhileNode *while_n = static_cast<const FSParser::WhileNode *>(s);

				codegen.start_block(); // Add an extra block, since we use custom logic to clear block locals.

				gen->start_while_condition();

				FSCodeGenerator::Address condition = _parse_expression(codegen, err, while_n->condition);
				if (err) {
					return err;
				}

				gen->write_while(condition);

				if (condition.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}

				// Loop variables must be cleared even when `break`/`continue` is used.
				List<FSCodeGenerator::Address> loop_locals = _add_block_locals(codegen, while_n->loop);

				_clear_block_locals(codegen, loop_locals); // Inside loop, before block - for `continue`.

				err = _parse_block(codegen, while_n->loop, false); // Don't add locals again.
				if (err) {
					return err;
				}

				gen->write_endwhile();

				_clear_block_locals(codegen, loop_locals); // Outside loop, after block - for `break` and normal exit.

				codegen.end_block(); // Get out of extra block for custom locals clearing.
			} break;
			case FSParser::Node::BREAK: {
				gen->write_break();
			} break;
			case FSParser::Node::CONTINUE: {
				gen->write_continue();
			} break;
			case FSParser::Node::RETURN: {
				const FSParser::ReturnNode *return_n = static_cast<const FSParser::ReturnNode *>(s);

				FSCodeGenerator::Address return_value;
				FSCodeGenerator::Address return_target;
				bool pop_return_target = false;

				if (return_n->return_value != nullptr) {
					return_value = _parse_expression(codegen, err, return_n->return_value);
					if (err) {
						return err;
					}
				}

				if (return_n->void_return) {
					// Always return "null", even if the expression is a call to a void function.
					gen->write_return(codegen.add_constant(Variant()));
				} else {
					return_target = return_value;
					if (codegen.function_node != nullptr && return_n->return_value != nullptr) {
						const FSDataType return_type = _gdtype_from_datatype(codegen.function_node->get_datatype(), codegen.script);
						if (_is_erased_container_call_to_typed_array(return_n->return_value, return_type)) {
							return_target = codegen.add_temporary(return_type);
							gen->write_assign_typed_array_convert(return_target, return_value);
							pop_return_target = true;
						} else if (_is_erased_container_call_to_typed_dictionary(return_n->return_value, return_type)) {
							return_target = codegen.add_temporary(return_type);
							gen->write_assign_typed_dictionary_convert(return_target, return_value);
							pop_return_target = true;
						}
					}
					gen->write_return(return_target);
				}
				if (pop_return_target) {
					codegen.generator->pop_temporary();
				}
				if (return_value.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}
			} break;
			case FSParser::Node::ASSERT: {
#ifdef DEBUG_ENABLED
				const FSParser::AssertNode *as = static_cast<const FSParser::AssertNode *>(s);

				FSCodeGenerator::Address condition = _parse_expression(codegen, err, as->condition);
				if (err) {
					return err;
				}

				FSCodeGenerator::Address message;

				if (as->message) {
					message = _parse_expression(codegen, err, as->message);
					if (err) {
						return err;
					}
				}
				gen->write_assert(condition, message);

				if (condition.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}
				if (message.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}
#endif
			} break;
			case FSParser::Node::BREAKPOINT: {
#ifdef DEBUG_ENABLED
				gen->write_breakpoint();
#endif
			} break;
			case FSParser::Node::VARIABLE: {
				const FSParser::VariableNode *lv = static_cast<const FSParser::VariableNode *>(s);
				// Should be already in stack when the block began.
				FSCodeGenerator::Address local = codegen.locals[lv->identifier->name];
				FSDataType local_type = _gdtype_from_datatype(lv->get_datatype(), codegen.script);

				bool initialized = false;
				if (lv->initializer != nullptr) {
					FSCodeGenerator::Address src_address = _parse_expression(codegen, err, lv->initializer);
					if (err) {
						return err;
					}
					if (_is_erased_container_call_to_typed_array(lv->initializer, local.type)) {
						gen->write_assign_typed_array_convert(local, src_address);
					} else if (_is_erased_container_call_to_typed_dictionary(lv->initializer, local.type)) {
						gen->write_assign_typed_dictionary_convert(local, src_address);
					} else if (lv->use_conversion_assign) {
						gen->write_assign_with_conversion(local, src_address);
					} else {
						gen->write_assign(local, src_address);
					}
					if (src_address.mode == FSCodeGenerator::Address::TEMPORARY) {
						codegen.generator->pop_temporary();
					}
					initialized = true;
				} else if (local_type.kind == FSDataType::BUILTIN || codegen.generator->is_local_dirty(local)) {
					// Initialize with default for the type. Built-in types must always be cleared (they cannot be `null`).
					// Objects and untyped variables are assigned to `null` only if the stack address has been reused and not cleared.
					codegen.generator->clear_address(local);
					initialized = true;
				}

				// Don't check `is_local_dirty()` since the variable must be assigned to `null` **on each iteration**.
				if (!initialized && p_block->is_in_loop) {
					codegen.generator->clear_address(local);
				}
			} break;
			case FSParser::Node::CONSTANT: {
				// Local constants.
				const FSParser::ConstantNode *lc = static_cast<const FSParser::ConstantNode *>(s);
				if (!lc->initializer->is_constant) {
					_set_error("Local constant must have a constant value as initializer.", lc->initializer);
					return ERR_PARSE_ERROR;
				}

				codegen.add_local_constant(lc->identifier->name,
						_resolve_aliased_class_constant(lc->initializer->reduced_value,
								_constant_storage_datatype(lc), codegen.script));
			} break;
			case FSParser::Node::PASS:
				// Nothing to do.
				break;
			default: {
				// Expression.
				if (s->is_expression()) {
					FSCodeGenerator::Address expr = _parse_expression(codegen, err, static_cast<const FSParser::ExpressionNode *>(s), true);
					if (err) {
						return err;
					}
					if (expr.mode == FSCodeGenerator::Address::TEMPORARY) {
						codegen.generator->pop_temporary();
					}
				} else {
					_set_error("Compiler bug (please report): unexpected node in parse tree while parsing statement.", s); // Unreachable code.
					return ERR_INVALID_DATA;
				}
			} break;
		}

		gen->clear_temporaries();
	}

	if (p_add_locals && p_clear_locals) {
		_clear_block_locals(codegen, block_locals);
	}

	codegen.end_block();
	return OK;
}

static HashMap<StringName, FSParser::DataType> _trait_type_argument_substitution(const FSParser::ClassNode *p_class, FSParser::ClassNode *p_trait) {
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
			HashMap<StringName, FSParser::DataType> inner = _trait_type_argument_substitution(used_trait, p_trait);
			if (inner.is_empty()) {
				continue;
			}
			const HashMap<StringName, FSParser::DataType> outer = _trait_type_argument_substitution(p_class, used_trait);
			for (const KeyValue<StringName, FSParser::DataType> &binding : inner) {
				bindings.insert(binding.key, FSParser::DataType::substitute(binding.value, outer));
			}
			return bindings;
		}
	}
	return bindings;
}

// Whether a class has static data of its own or flattens static data in from an
// applied trait. Trait static data is recompiled into the implementer, so it must be
// accounted for wherever the class's own static data is.
static bool _class_or_traits_have_static_data(const FSParser::ClassNode *p_class) {
	if (p_class->has_static_data) {
		return true;
	}
	for (FSParser::ClassNode *trait : p_class->resolved_traits) {
		if (trait != nullptr && trait->has_static_data) {
			return true;
		}
	}
	return false;
}

// Returns whether a trait member of the given kind is flattened into an implementer.
// Variables, constants, enums, enum values, signals, and concrete functions are
// flattened; abstract (required) functions are contracts the implementer satisfies
// rather than bodies to copy, and inner classes and export groups are not flattened.
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

// Converts an AST node's resolved annotation list into runtime-safe passive metadata, preserving
// source order. Both Godot's built-in annotations (`@export`, `@rpc`, `@onready`, ...) and custom
// annotations the analyzer resolved to a declaration are persisted; the resulting usages carry
// `is_builtin` to discriminate them. Argument values come from the analyzer's reduced constants:
// for custom annotations, positionally-supplied values go to `args` and named ones to `kwargs` keyed
// by parameter name; built-ins are positional-only, so all of their resolved arguments go to `args`.
// The analyzer keeps `resolved_arguments` parallel to `argument_names` for a clean compile; the size
// guard keeps this defensive against any partially-resolved node.
//
// Script-configuration annotations the parser applies and discards (`@tool`, `@icon`,
// `@static_unload`) and standalone layout markers (`@export_group`/`@export_category` and the
// `@warning_ignore_start`/`@warning_ignore_restore` pair) never reach a declaration's annotation list,
// so they are not reflected; only built-ins retained on a class/member declaration are surfaced.
void FSCompiler::_collect_annotations(const List<FSParser::AnnotationNode *> &p_annotations, Vector<FoundryScript::AnnotationUsage> &r_usages) {
	for (const FSParser::AnnotationNode *annotation : p_annotations) {
		if (annotation == nullptr) {
			continue;
		}

		FoundryScript::AnnotationUsage usage;
		if (annotation->is_custom) {
			// A custom usage only carries persistable metadata once the analyzer resolved it to a
			// declaration; unresolved usages (an analysis error) are skipped.
			if (annotation->resolved_qualified_name.is_empty()) {
				continue;
			}
			usage.is_builtin = false;
			// Usage nodes carry the spelled name including the leading "@". A fully qualified usage such
			// as `@cafecito.test.timeout` spells the whole identity, so persist only the final segment as
			// the bare short name; the canonical identity is kept separately in `qualified_name`.
			String short_name = String(annotation->name).trim_prefix("@");
			const int last_dot = short_name.rfind_char('.');
			if (last_dot >= 0) {
				short_name = short_name.substr(last_dot + 1);
			}
			usage.name = short_name;
			usage.qualified_name = annotation->resolved_qualified_name;

			for (int i = 0; i < annotation->resolved_arguments.size(); i++) {
				const StringName argument_name = i < annotation->argument_names.size() ? annotation->argument_names[i] : StringName();
				if (argument_name == StringName()) {
					usage.args.push_back(annotation->resolved_arguments[i]);
				} else {
					usage.kwargs[argument_name] = annotation->resolved_arguments[i];
				}
			}
		} else {
			// Built-in annotation. A null `info` means the parser left an unknown name for the analyzer
			// to reject, so it carries no built-in identity to persist.
			if (annotation->info == nullptr) {
				continue;
			}
			// `@warning_ignore` is a diagnostic-suppression directive, not declarative metadata: the
			// analyzer only resolves its arguments under DEBUG_ENABLED (and the normal member loop skips
			// it), so reflecting it would expose build-configuration-dependent data (arguments present
			// in dev builds, empty in release). Exclude it to keep reflected metadata consistent.
			if (annotation->name == SNAME("@warning_ignore")) {
				continue;
			}
			usage.is_builtin = true;
			// Built-in annotations are never dotted; the bare name doubles as the canonical identity so
			// `has_annotation`/`get_annotation` match either accessor.
			const StringName short_name = String(annotation->name).trim_prefix("@");
			usage.name = short_name;
			usage.qualified_name = short_name;
			// Built-in annotations are positional-only; the analyzer leaves `argument_names` empty.
			for (const Variant &argument : annotation->resolved_arguments) {
				usage.args.push_back(argument);
			}
		}

		r_usages.push_back(usage);
	}
}

// Collects the trait members to flatten into an implementing class, in declaration
// order across the class's transitively-resolved trait set. A member is skipped when a
// member with the same name is already defined by the implementer or any of its
// (FoundryScript) base classes — an explicit override that shadows the trait, matching how
// the analyzer resolves such names — or when an earlier trait already contributed it,
// so a diamond-reached trait is included exactly once.
//
// The trait member nodes are owned by other parsers' trees (for external traits); they
// remain valid only while those parsers stay cached for the duration of compilation,
// the same lifetime assumption the trait analyzer already relies on.
void FSCompiler::_collect_flattened_trait_members(const FSParser::ClassNode *p_class, Vector<const FSParser::ClassNode::Member *> &r_members) {
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
			if (defined.has(name)) {
				continue;
			}
			defined.insert(name);
			r_members.push_back(&member);
		}
	}
}

// Records the abstract method requirements contributed by the traits `p_class`
// (transitively) uses but does not flatten into its own members. Abstract trait
// members are contracts the implementer satisfies, not bodies that are copied in,
// and at runtime a class retains only trait identity names — so without this a
// dynamic proxy of a sub-trait would miss requirements inherited through a `uses`
// chain and let those calls fall through to native dispatch. A requirement is
// skipped when a concrete member (declared by the implementer or a base, or a
// concrete trait member flattened in) already provides it, matching the shadowing
// rules in `_collect_flattened_trait_members`. Diamond-reached requirements are
// recorded once; they share the same declared signature, so the first writer wins.
void FSCompiler::_collect_trait_abstract_requirements(const FSParser::ClassNode *p_class, FoundryScript *p_script) {
	p_script->abstract_trait_requirements.clear();
	if (p_class->resolved_traits.is_empty()) {
		return;
	}

	// A requirement is already satisfied only when a same-named *callable* will live in
	// `member_functions`, where the proxy's `_find_contract_function` can reach it. That
	// is exactly the implementer's and bases' own methods plus the concrete trait
	// methods that are actually flattened in. A non-function member (var, constant,
	// enum, signal) does not satisfy a method contract, and a concrete trait method that
	// flattening drops because an earlier member already claimed the name never becomes a
	// callable — so neither may suppress the requirement. `_collect_flattened_trait_members`
	// applies the same shadowing the compiler uses, so reuse it rather than re-deriving.
	HashSet<StringName> provided;
	for (const FSParser::ClassNode *owner = p_class; owner != nullptr; owner = owner->base_type.class_type) {
		for (const FSParser::ClassNode::Member &member : owner->members) {
			if (member.type != FSParser::ClassNode::Member::FUNCTION) {
				continue;
			}
			const StringName name = member.get_name();
			if (name != StringName()) {
				provided.insert(name);
			}
		}
	}
	Vector<const FSParser::ClassNode::Member *> flattened;
	_collect_flattened_trait_members(p_class, flattened);
	for (const FSParser::ClassNode::Member *member : flattened) {
		if (member->type != FSParser::ClassNode::Member::FUNCTION || member->function == nullptr || member->function->is_abstract) {
			continue;
		}
		const StringName name = member->get_name();
		if (name != StringName()) {
			provided.insert(name);
		}
	}

	for (FSParser::ClassNode *trait : p_class->resolved_traits) {
		if (trait == nullptr) {
			continue;
		}
		for (const FSParser::ClassNode::Member &member : trait->members) {
			if (member.type != FSParser::ClassNode::Member::FUNCTION) {
				continue;
			}
			const FSParser::FunctionNode *function = member.function;
			if (function == nullptr || !function->is_abstract) {
				continue;
			}
			const StringName name = member.get_name();
			if (name == StringName() || provided.has(name) || p_script->abstract_trait_requirements.has(name)) {
				continue;
			}

			FoundryScript::AbstractTraitRequirement requirement;

			// Build the enumerable signature, mirroring how `_parse_function` fills a
			// compiled method's `MethodInfo` and return type from the same parser node,
			// so the proxy's contract resolution and `get_method_list` agree with what a
			// direct proxy of the declaring trait sees.
			MethodInfo &method_info = requirement.method_info;
			method_info.name = name;
			method_info.flags |= METHOD_FLAG_VIRTUAL_REQUIRED;
			if (function->is_static) {
				method_info.flags |= METHOD_FLAG_STATIC;
			}
			if (function->is_coroutine) {
				method_info.flags |= METHOD_FLAG_ASYNC;
			}
			for (int i = 0; i < function->parameters.size(); i++) {
				const FSParser::ParameterNode *parameter = function->parameters[i];
				const FSParser::DataType parameter_datatype = _substitute_self_type_parameter_for_class(parameter->get_datatype(), p_class);
				method_info.arguments.push_back(parameter_datatype.to_property_info(parameter->identifier->name));
			}
			if (function->is_vararg()) {
				method_info.flags |= METHOD_FLAG_VARARG;
			}
			method_info.default_arguments.append_array(function->default_arg_values);

			// Same rule `_parse_function` applies: an abstract method contributes a
			// return type only when it declares one explicitly (or, defensively, has a
			// returning body); an unannotated requirement is `void`, not `Variant`, so
			// the handler's value is ignored just as for the compiled function.
			if ((function->is_abstract && function->return_type != nullptr) || function->body->has_return) {
				const FSParser::DataType return_datatype = _substitute_self_type_parameter_for_class(function->get_datatype(), p_class);
				requirement.return_type = _gdtype_from_datatype(return_datatype, p_script);
				method_info.return_val = return_datatype.to_property_info(String());
			} else {
				requirement.return_type.kind = FSDataType::BUILTIN;
				requirement.return_type.builtin_type = Variant::NIL;
			}

			p_script->abstract_trait_requirements.insert(name, requirement);
		}
	}
}

FSFunction *FSCompiler::_parse_function(Error &r_error, FoundryScript *p_script, const FSParser::ClassNode *p_class, const FSParser::FunctionNode *p_func, bool p_for_ready, bool p_for_lambda) {
	r_error = OK;
	CodeGen codegen;
	codegen.generator = memnew(FSByteCodeGenerator);

	codegen.class_node = p_class;
	codegen.script = p_script;
	codegen.function_node = p_func;

	StringName func_name;
	bool is_abstract = false;
	bool is_static = false;
	Variant rpc_config;
	FSDataType return_type;
	FSParser::DataType function_datatype;
	return_type.kind = FSDataType::BUILTIN;
	return_type.builtin_type = Variant::NIL;

	if (p_func) {
		if (p_func->identifier) {
			func_name = p_func->identifier->name;
		} else {
			func_name = "<anonymous lambda>";
		}
		is_abstract = p_func->is_abstract;
		is_static = p_func->is_static;
		rpc_config = p_func->rpc_config;
		function_datatype = _substitute_self_type_parameter_for_class(p_func->get_datatype(), p_class);
		return_type = _gdtype_from_datatype(function_datatype, p_script);
	} else {
		if (p_for_ready) {
			func_name = "@implicit_ready";
		} else {
			func_name = "@implicit_new";
		}
	}

	MethodInfo method_info;

	codegen.function_name = func_name;
	method_info.name = func_name;
	codegen.is_static = is_static;
	if (is_abstract) {
		method_info.flags |= METHOD_FLAG_VIRTUAL_REQUIRED;
	}
	if (is_static) {
		method_info.flags |= METHOD_FLAG_STATIC;
	}
	if (p_func && p_func->is_coroutine) {
		method_info.flags |= METHOD_FLAG_ASYNC;
	}
	codegen.generator->write_start(p_script, func_name, is_static, rpc_config, return_type);

	int optional_parameters = 0;
	FSCodeGenerator::Address vararg_addr;

	if (p_func) {
		for (int i = 0; i < p_func->parameters.size(); i++) {
			const FSParser::ParameterNode *parameter = p_func->parameters[i];
			const FSParser::DataType parameter_datatype = _substitute_self_type_parameter_for_class(parameter->get_datatype(), p_class);
			FSDataType par_type = _gdtype_from_datatype(parameter_datatype, p_script);
			uint32_t par_addr = codegen.generator->add_parameter(parameter->identifier->name, parameter->initializer != nullptr, par_type);
			codegen.parameters[parameter->identifier->name] = FSCodeGenerator::Address(FSCodeGenerator::Address::FUNCTION_PARAMETER, par_addr, par_type);

			method_info.arguments.push_back(parameter_datatype.to_property_info(parameter->identifier->name));

			if (parameter->initializer != nullptr) {
				optional_parameters++;
			}
		}

		if (p_func->is_vararg()) {
			vararg_addr = codegen.add_local(p_func->rest_parameter->identifier->name, _gdtype_from_datatype(p_func->rest_parameter->get_datatype(), codegen.script));
			method_info.flags |= METHOD_FLAG_VARARG;
		}

		method_info.default_arguments.append_array(p_func->default_arg_values);
	}

	// Parse initializer if applies.
	bool is_implicit_initializer = !p_for_ready && !p_func && !p_for_lambda;
	bool is_initializer = p_func && !p_for_lambda && p_func->identifier->name == FSLanguage::get_singleton()->strings._init;
	bool is_implicit_ready = !p_func && p_for_ready;

	// The implicit initializer (and `@implicit_ready()`) must construct and initialize
	// the variables flattened in from applied traits as well as the class's own, so
	// trait state is set up per implementer.
	Vector<const FSParser::ClassNode::Member *> initializer_members;
	if (!p_for_lambda && (is_implicit_initializer || is_implicit_ready)) {
		for (int i = 0; i < p_class->members.size(); i++) {
			initializer_members.push_back(&p_class->members[i]);
		}
		_collect_flattened_trait_members(p_class, initializer_members);
	}

	if (!p_for_lambda && is_implicit_initializer) {
		// Initialize the default values for typed variables before anything.
		// This avoids crashes if they are accessed with validated calls before being properly initialized.
		// It may happen with out-of-order access or with `@onready` variables.
		for (const FSParser::ClassNode::Member *member_ptr : initializer_members) {
			const FSParser::ClassNode::Member &member = *member_ptr;
			if (member.type != FSParser::ClassNode::Member::VARIABLE) {
				continue;
			}

			const FSParser::VariableNode *field = member.variable;
			if (field->is_static) {
				continue;
			}

			FSDataType field_type = _gdtype_from_datatype(field->get_datatype(), codegen.script);
			if (field_type.has_type()) {
				codegen.generator->write_newline(field->start_line);

				FSCodeGenerator::Address dst_address(FSCodeGenerator::Address::MEMBER, codegen.script->member_indices[field->identifier->name].index, field_type);

				if (field_type.builtin_type == Variant::ARRAY && field_type.has_container_element_type(0)) {
					codegen.generator->write_construct_typed_array(dst_address, field_type.get_container_element_type(0), Vector<FSCodeGenerator::Address>());
				} else if (field_type.builtin_type == Variant::DICTIONARY && field_type.has_container_element_types()) {
					codegen.generator->write_construct_typed_dictionary(dst_address, field_type.get_container_element_type_or_variant(0),
							field_type.get_container_element_type_or_variant(1), Vector<FSCodeGenerator::Address>());
				} else if (field_type.kind == FSDataType::BUILTIN) {
					codegen.generator->write_construct(dst_address, field_type.builtin_type, Vector<FSCodeGenerator::Address>());
				}
				// The `else` branch is for objects, in such case we leave it as `null`.
			}
		}
	}

	if (!p_for_lambda && (is_implicit_initializer || is_implicit_ready)) {
		// Initialize class fields.
		for (const FSParser::ClassNode::Member *member_ptr : initializer_members) {
			if (member_ptr->type != FSParser::ClassNode::Member::VARIABLE) {
				continue;
			}
			const FSParser::VariableNode *field = member_ptr->variable;
			if (field->is_static) {
				continue;
			}

			if (field->onready != is_implicit_ready) {
				// Only initialize in `@implicit_ready()`.
				continue;
			}

			if (field->initializer) {
				codegen.generator->write_newline(field->initializer->start_line);

				FSCodeGenerator::Address src_address = _parse_expression(codegen, r_error, field->initializer, false, true);
				if (r_error) {
					memdelete(codegen.generator);
					return nullptr;
				}

				const FoundryScript::MemberInfo &field_minfo = codegen.script->member_indices[field->identifier->name];
				FSDataType field_type = _gdtype_from_datatype(field->get_datatype(), codegen.script);
				FSCodeGenerator::Address dst_address(FSCodeGenerator::Address::MEMBER, field_minfo.index, field_type);

				if (field_minfo.type_argument_binding.kind != FoundryScript::TypeArgumentBinding::NONE) {
					// A `T`-typed field initializer stores directly into the erased member slot; validate
					// it against the binding the leaf script resolved for this member slot at runtime.
					codegen.generator->write_assign_typed_parameter(dst_address, src_address, field_minfo.index);
				} else if (_is_erased_container_call_to_typed_array(field->initializer, field_type)) {
					// The initializer is a generic method returning an erased `Array[T]`; retype the
					// untyped runtime array into the concrete element type rather than strict-validating it.
					codegen.generator->write_assign_typed_array_convert(dst_address, src_address);
				} else if (_is_erased_container_call_to_typed_dictionary(field->initializer, field_type)) {
					// The initializer is a generic method returning an erased `Dictionary[K, V]`; retype the
					// untyped runtime dictionary into the concrete key/value types rather than strict-validating it.
					codegen.generator->write_assign_typed_dictionary_convert(dst_address, src_address);
				} else if (field->use_conversion_assign) {
					codegen.generator->write_assign_with_conversion(dst_address, src_address);
				} else {
					codegen.generator->write_assign(dst_address, src_address);
				}
				if (src_address.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}
			}
		}
	}

	// Parse default argument code if applies.
	if (p_func) {
		if (optional_parameters > 0) {
			codegen.generator->start_parameters();
			for (int i = p_func->parameters.size() - optional_parameters; i < p_func->parameters.size(); i++) {
				const FSParser::ParameterNode *parameter = p_func->parameters[i];
				FSCodeGenerator::Address src_addr = _parse_expression(codegen, r_error, parameter->initializer);
				if (r_error) {
					memdelete(codegen.generator);
					return nullptr;
				}
				FSCodeGenerator::Address dst_addr = codegen.parameters[parameter->identifier->name];
				codegen.generator->write_assign_default_parameter(dst_addr, src_addr, parameter->use_conversion_assign);
				if (src_addr.mode == FSCodeGenerator::Address::TEMPORARY) {
					codegen.generator->pop_temporary();
				}
			}
			codegen.generator->end_parameters();
		}

		// No need to reset locals at the end of the function, the stack will be cleared anyway.
		r_error = _parse_block(codegen, p_func->body, true, false);
		if (r_error) {
			memdelete(codegen.generator);
			return nullptr;
		}
	}

#ifdef DEBUG_ENABLED
	// Record the profiler signature in every debug build, not only when a
	// debugger is attached, so headless profiling tools (e.g. the FoundryScript
	// benchmark profiler) can label per-function timing. The signature is inert
	// unless the function profiler is running.
	{
		String signature;
		// Path.
		if (!p_script->get_script_path().is_empty()) {
			signature += p_script->get_script_path();
		}
		// Location.
		if (p_func) {
			signature += "::" + itos(p_func->body->start_line);
		} else {
			signature += "::0";
		}

		// Function and class.

		if (p_class->identifier) {
			signature += "::" + String(p_class->identifier->name) + "." + String(func_name);
		} else {
			signature += "::" + String(func_name);
		}

		if (p_for_lambda) {
			signature += "(lambda)";
		}

		codegen.generator->set_signature(signature);
	}
#endif

	if (p_func) {
		codegen.generator->set_initial_line(p_func->start_line);
	} else {
		codegen.generator->set_initial_line(0);
	}

	FSFunction *gd_function = codegen.generator->write_end();

	if (is_initializer) {
		p_script->initializer = gd_function;
	} else if (is_implicit_initializer) {
		p_script->implicit_initializer = gd_function;
	} else if (is_implicit_ready) {
		p_script->implicit_ready = gd_function;
	}

	if (p_func) {
		// Abstract functions have no executable body, but MethodInfo must still expose annotated return contracts.
		if ((p_func->is_abstract && p_func->return_type != nullptr) || p_func->body->has_return) {
			gd_function->return_type = _gdtype_from_datatype(function_datatype, p_script);
			method_info.return_val = function_datatype.to_property_info(String());
		} else {
			// If no `return` statement, then return type is `void`, not `Variant`.
			gd_function->return_type = FSDataType();
			gd_function->return_type.kind = FSDataType::BUILTIN;
			gd_function->return_type.builtin_type = Variant::NIL;
		}

		if (p_func->is_vararg()) {
			gd_function->_vararg_index = vararg_addr.address;
		}
	}

	gd_function->method_info = method_info;

	if (!is_implicit_initializer && !is_implicit_ready && !p_for_lambda) {
		p_script->member_functions[func_name] = gd_function;
	}

	memdelete(codegen.generator);

	return gd_function;
}

FSFunction *FSCompiler::_make_static_initializer(Error &r_error, FoundryScript *p_script, const FSParser::ClassNode *p_class) {
	r_error = OK;
	CodeGen codegen;
	codegen.generator = memnew(FSByteCodeGenerator);

	codegen.class_node = p_class;
	codegen.script = p_script;

	StringName func_name = SNAME("@static_initializer");
	bool is_static = true;
	Variant rpc_config;
	FSDataType return_type;
	return_type.kind = FSDataType::BUILTIN;
	return_type.builtin_type = Variant::NIL;

	codegen.function_name = func_name;
	codegen.is_static = is_static;
	codegen.generator->write_start(p_script, func_name, is_static, rpc_config, return_type);

	// The static initializer is always called on the same class where the static variables are defined,
	// so the CLASS address (current class) can be used instead of `codegen.add_constant(p_script)`.
	FSCodeGenerator::Address class_addr(FSCodeGenerator::Address::CLASS);

	// Static variables flattened in from applied traits are initialized here too.
	Vector<const FSParser::ClassNode::Member *> static_members;
	for (int i = 0; i < p_class->members.size(); i++) {
		static_members.push_back(&p_class->members[i]);
	}
	_collect_flattened_trait_members(p_class, static_members);

	// Initialize the default values for typed variables before anything.
	// This avoids crashes if they are accessed with validated calls before being properly initialized.
	// It may happen with out-of-order access or with `@onready` variables.
	for (const FSParser::ClassNode::Member *member_ptr : static_members) {
		const FSParser::ClassNode::Member &member = *member_ptr;
		if (member.type != FSParser::ClassNode::Member::VARIABLE) {
			continue;
		}

		const FSParser::VariableNode *field = member.variable;
		if (!field->is_static) {
			continue;
		}

		FSDataType field_type = _gdtype_from_datatype(field->get_datatype(), codegen.script);
		if (field_type.has_type()) {
			codegen.generator->write_newline(field->start_line);

			if (field_type.builtin_type == Variant::ARRAY && field_type.has_container_element_type(0)) {
				FSCodeGenerator::Address temp = codegen.add_temporary(field_type);
				codegen.generator->write_construct_typed_array(temp, field_type.get_container_element_type(0), Vector<FSCodeGenerator::Address>());
				codegen.generator->write_set_static_variable(temp, class_addr, p_script->static_variables_indices[field->identifier->name].index);
				codegen.generator->pop_temporary();
			} else if (field_type.builtin_type == Variant::DICTIONARY && field_type.has_container_element_types()) {
				FSCodeGenerator::Address temp = codegen.add_temporary(field_type);
				codegen.generator->write_construct_typed_dictionary(temp, field_type.get_container_element_type_or_variant(0),
						field_type.get_container_element_type_or_variant(1), Vector<FSCodeGenerator::Address>());
				codegen.generator->write_set_static_variable(temp, class_addr, p_script->static_variables_indices[field->identifier->name].index);
				codegen.generator->pop_temporary();
			} else if (field_type.kind == FSDataType::BUILTIN) {
				FSCodeGenerator::Address temp = codegen.add_temporary(field_type);
				codegen.generator->write_construct(temp, field_type.builtin_type, Vector<FSCodeGenerator::Address>());
				codegen.generator->write_set_static_variable(temp, class_addr, p_script->static_variables_indices[field->identifier->name].index);
				codegen.generator->pop_temporary();
			}
			// The `else` branch is for objects, in such case we leave it as `null`.
		}
	}

	for (const FSParser::ClassNode::Member *member_ptr : static_members) {
		// Initialize static fields.
		if (member_ptr->type != FSParser::ClassNode::Member::VARIABLE) {
			continue;
		}
		const FSParser::VariableNode *field = member_ptr->variable;
		if (!field->is_static) {
			continue;
		}

		if (field->initializer) {
			codegen.generator->write_newline(field->initializer->start_line);

			FSCodeGenerator::Address src_address = _parse_expression(codegen, r_error, field->initializer, false, true);
			if (r_error) {
				memdelete(codegen.generator);
				return nullptr;
			}

			FSDataType field_type = _gdtype_from_datatype(field->get_datatype(), codegen.script);
			FSCodeGenerator::Address temp = codegen.add_temporary(field_type);

			if (_is_erased_container_call_to_typed_array(field->initializer, field_type)) {
				// The initializer is a generic method returning an erased `Array[T]`; retype the
				// untyped runtime array into the concrete element type rather than strict-validating it.
				codegen.generator->write_assign_typed_array_convert(temp, src_address);
			} else if (_is_erased_container_call_to_typed_dictionary(field->initializer, field_type)) {
				// The initializer is a generic method returning an erased `Dictionary[K, V]`; retype the
				// untyped runtime dictionary into the concrete key/value types rather than strict-validating it.
				codegen.generator->write_assign_typed_dictionary_convert(temp, src_address);
			} else if (field->use_conversion_assign) {
				codegen.generator->write_assign_with_conversion(temp, src_address);
			} else {
				codegen.generator->write_assign(temp, src_address);
			}
			if (src_address.mode == FSCodeGenerator::Address::TEMPORARY) {
				codegen.generator->pop_temporary();
			}

			codegen.generator->write_set_static_variable(temp, class_addr, p_script->static_variables_indices[field->identifier->name].index);
			codegen.generator->pop_temporary();
		}
	}

	if (p_script->has_method(FSLanguage::get_singleton()->strings._static_init)) {
		codegen.generator->write_newline(p_class->start_line);
		codegen.generator->write_call(FSCodeGenerator::Address(), class_addr, FSLanguage::get_singleton()->strings._static_init, Vector<FSCodeGenerator::Address>());
	}

#ifdef DEBUG_ENABLED
	// Record the profiler signature in every debug build, not only when a
	// debugger is attached, so headless profiling tools (e.g. the FoundryScript
	// benchmark profiler) can label per-function timing. The signature is inert
	// unless the function profiler is running.
	{
		String signature;
		// Path.
		if (!p_script->get_script_path().is_empty()) {
			signature += p_script->get_script_path();
		}
		// Location.
		signature += "::0";

		// Function and class.

		if (p_class->identifier) {
			signature += "::" + String(p_class->identifier->name) + "." + String(func_name);
		} else {
			signature += "::" + String(func_name);
		}

		codegen.generator->set_signature(signature);
	}
#endif

	codegen.generator->set_initial_line(p_class->start_line);

	FSFunction *gd_function = codegen.generator->write_end();

	memdelete(codegen.generator);

	return gd_function;
}

Error FSCompiler::_parse_setter_getter(FoundryScript *p_script, const FSParser::ClassNode *p_class, const FSParser::VariableNode *p_variable, bool p_is_setter) {
	Error err = OK;

	FSParser::FunctionNode *function;

	if (p_is_setter) {
		function = p_variable->setter;
	} else {
		function = p_variable->getter;
	}

	_parse_function(err, p_script, p_class, function);

	return err;
}

// Prepares given script, and inner class scripts, for compilation. It populates class members and
// initializes method RPC info for its base classes first, then for itself, then for inner classes.
// WARNING: This function cannot initiate compilation of other classes, or it will result in
// cyclic dependency issues.
void FSCompiler::_specialize_type_argument_binding(FoundryScript::TypeArgumentBinding &r_binding, const Vector<FSParser::DataType> &p_base_specialization, FoundryScript *p_owner) {
	if (r_binding.kind != FoundryScript::TypeArgumentBinding::OPEN) {
		return; // FIXED stays fixed; NONE is not a type-parameter binding.
	}
	const int base_ordinal = r_binding.leaf_ordinal; // Open relative to the base's parameters.
	if (base_ordinal < 0 || base_ordinal >= p_base_specialization.size()) {
		return; // Base not specialized at this ordinal (e.g. raw `extends Base`); leave open.
	}
	const FSParser::DataType &argument = p_base_specialization[base_ordinal];
	if (argument.kind == FSParser::DataType::TYPE_PARAMETER &&
			argument.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS) {
		r_binding.leaf_ordinal = argument.type_parameter_index; // Forwarded to this class's parameter.
	} else {
		r_binding.kind = FoundryScript::TypeArgumentBinding::FIXED;
		r_binding.fixed = _gdtype_from_datatype(argument, p_owner, false);
		// A composite argument that still mentions an open parameter (`extends Box[Array[T]]`) is erased
		// by `_gdtype_from_datatype`, so the baked type no longer reflects the dependent reification. Flag
		// it so the leaf-to-base projection refrains from validating that slot rather than rejecting it.
		r_binding.fixed_is_dependent = _datatype_contains_erased_type_parameter(argument);
		r_binding.leaf_ordinal = -1;
	}
}

Variant FSCompiler::_resolve_aliased_class_constant(const Variant &p_value) {
	if (p_value.get_type() != Variant::OBJECT || main_script == nullptr) {
		return p_value;
	}
	FoundryScript *folded_class = Object::cast_to<FoundryScript>(p_value);
	if (folded_class == nullptr || folded_class->is_valid()) {
		// Not a class, or an already-compiled class (e.g. an external preload). Leave it as-is.
		return p_value;
	}
	FoundryScript *live_class = main_script->find_class(folded_class->get_fully_qualified_name());
	// Only re-point a class that genuinely belongs to this compilation unit. `find_class` resolves a
	// fully-qualified name against `main_script`'s subclass tree by matching its leading path prefix,
	// so an unrelated external script whose path happened to prefix-collide could otherwise resolve to
	// a same-named local class. Confirming the resolved class's fully-qualified name matches the folded
	// one rejects such a collision: a true same-unit alias matches exactly, an external one does not.
	if (live_class != nullptr && live_class->get_fully_qualified_name() == folded_class->get_fully_qualified_name()) {
		// Re-point at the live class compiled in this unit — the same object the inner-class name
		// itself resolves to. Mirrors the specialized-handle re-resolution from #242.
		return Ref<FoundryScript>(live_class);
	}
	return p_value;
}

Variant FSCompiler::_resolve_aliased_class_constant(const Variant &p_value,
		const FSParser::DataType &p_datatype, FoundryScript *p_owner) {
	Variant resolved = _resolve_aliased_class_constant(p_value);
	if (!p_datatype.is_meta_type || p_datatype.kind != FSParser::DataType::CLASS ||
			p_datatype.type_arguments.is_empty()) {
		return resolved;
	}

	FoundryScript *base_class = Object::cast_to<FoundryScript>(resolved.operator Object *());
	if (base_class == nullptr) {
		return resolved;
	}

	Vector<ContainerType> type_arguments;
	for (const FSParser::DataType &argument : p_datatype.type_arguments) {
		type_arguments.push_back(_gdtype_from_datatype(argument, p_owner).to_container_type());
	}
	return FSSpecializedClassHandle::create(Ref<FoundryScript>(base_class), type_arguments);
}

Error FSCompiler::_prepare_compilation(FoundryScript *p_script, const FSParser::ClassNode *p_class, bool p_keep_state) {
	if (parsed_classes.has(p_script)) {
		return OK;
	}

	if (parsing_classes.has(p_script)) {
		String class_name = p_class->identifier ? String(p_class->identifier->name) : p_class->fqcn;
		_set_error(vformat(R"(Cyclic class reference for "%s".)", class_name), p_class);
		return ERR_PARSE_ERROR;
	}

	parsing_classes.insert(p_script);

	p_script->clearing = true;

	p_script->cancel_pending_functions(true);

	p_script->native = Ref<FSNativeClass>();
	p_script->base = Ref<FoundryScript>();
	p_script->members.clear();
	p_script->script_trait_list.clear();
	p_script->abstract_trait_requirements.clear();
	p_script->type_parameters.clear();

	// This makes possible to clear script constants and member_functions without heap-use-after-free errors.
	HashMap<StringName, Variant> constants;
	for (const KeyValue<StringName, Variant> &E : p_script->constants) {
		constants.insert(E.key, E.value);
	}
	p_script->constants.clear();
	constants.clear();
	HashMap<StringName, FSFunction *> member_functions;
	for (const KeyValue<StringName, FSFunction *> &E : p_script->member_functions) {
		member_functions.insert(E.key, E.value);
	}
	p_script->member_functions.clear();
	for (const KeyValue<StringName, FSFunction *> &E : member_functions) {
		memdelete(E.value);
	}
	member_functions.clear();

	p_script->static_variables.clear();

	if (p_script->implicit_initializer) {
		memdelete(p_script->implicit_initializer);
	}
	if (p_script->implicit_ready) {
		memdelete(p_script->implicit_ready);
	}
	if (p_script->static_initializer) {
		memdelete(p_script->static_initializer);
	}

	p_script->member_functions.clear();
	p_script->member_indices.clear();
	p_script->static_variables_indices.clear();
	p_script->static_variables.clear();
	p_script->_signals.clear();
	p_script->initializer = nullptr;
	p_script->implicit_initializer = nullptr;
	p_script->implicit_ready = nullptr;
	p_script->static_initializer = nullptr;
	p_script->rpc_config.clear();
	p_script->lambda_info.clear();
	p_script->class_annotations.clear();
	p_script->method_annotations.clear();
	p_script->variable_annotations.clear();
	p_script->signal_annotations.clear();
	p_script->constant_annotations.clear();

	p_script->clearing = false;

	p_script->tool = parser->is_tool();
	p_script->_is_abstract = p_class->is_abstract;
	p_script->_is_final = p_class->is_final;
	p_script->_is_trait_type = p_class->is_trait;
	p_script->trait_type_name = p_class->is_trait ? fs_trait_identity_name(p_class) : StringName();

	// Class annotations are direct-only (not inherited); record this class's own resolved usages.
	_collect_annotations(p_class->annotations, p_script->class_annotations);

	if (p_script->local_name != StringName()) {
		if (FSAnalyzer::class_exists(p_script->local_name)) {
			_set_error(vformat(R"(The class "%s" shadows a native class)", p_script->local_name), p_class);
			return ERR_ALREADY_EXISTS;
		}
	}

	FSDataType base_type = _gdtype_from_datatype(p_class->base_type, p_script, false);

	if (base_type.native_type == StringName()) {
		_set_error(vformat(R"(Parser bug (please report): Empty native type in base class "%s")", p_script->path), p_class);
		return ERR_BUG;
	}

	int native_idx = FSLanguage::get_singleton()->get_global_map()[base_type.native_type];

	p_script->native = FSLanguage::get_singleton()->get_global_array()[native_idx];
	if (p_script->native.is_null()) {
		_set_error("Compiler bug (please report): script native type is null.", nullptr);
		return ERR_BUG;
	}

	// Inheritance
	switch (base_type.kind) {
		case FSDataType::NATIVE:
			// Nothing more to do.
			break;
		case FSDataType::FOUNDRY_SCRIPT: {
			Ref<FoundryScript> base = Ref<FoundryScript>(base_type.script_type);
			if (base.is_null()) {
				_set_error("Compiler bug (please report): base script type is null.", nullptr);
				return ERR_BUG;
			}

			if (main_script->has_class(base.ptr())) {
				Error err = _prepare_compilation(base.ptr(), p_class->base_type.class_type, p_keep_state);
				if (err) {
					return err;
				}
			} else if (!base->is_valid()) {
				Error err = OK;
				Ref<FoundryScript> base_root = FSCache::get_shallow_script(base->path, err, p_script->path);
				if (err) {
					_set_error(vformat(R"(Could not parse base class "%s" from "%s": %s)", base->fully_qualified_name, base->path, error_names[err]), nullptr);
					return err;
				}
				if (base_root.is_valid()) {
					base = Ref<FoundryScript>(base_root->find_class(base->fully_qualified_name));
				}
				if (base.is_null()) {
					_set_error(vformat(R"(Could not find class "%s" in "%s".)", base->fully_qualified_name, base->path), nullptr);
					return ERR_COMPILATION_FAILED;
				}

				err = _prepare_compilation(base.ptr(), p_class->base_type.class_type, p_keep_state);
				if (err) {
					_set_error(vformat(R"(Could not populate class members of base class "%s" in "%s".)", base->fully_qualified_name, base->path), nullptr);
					return err;
				}
			}

			// Backstop for dynamically loaded scripts that bypass the analyzer: a final base
			// cannot be extended. The analyzer rejects this for statically analyzed scripts;
			// this load-time guard covers the runtime `load()`/`reload()` path.
			if (base->is_final()) {
				_set_error(vformat(R"(Cannot extend final class "%s".)", base->fully_qualified_name.is_empty() ? base->path : base->fully_qualified_name), p_class);
				return ERR_PARSE_ERROR;
			}

			p_script->base = base;
			p_script->member_indices = base->member_indices;

			// Re-resolve each inherited type-parameter member's binding through this class's
			// `extends Base[args]` specialization. A base member still open relative to the base's
			// parameters is either fixed to a concrete argument (`extends Base[int]` → FIXED) or
			// forwarded to one of this class's own parameters (`extends Base[T]` → remapped OPEN), so it
			// reifies against the correct argument rather than blindly indexing the leaf's own.
			const Vector<FSParser::DataType> &base_specialization = p_class->base_type.type_arguments;
			for (KeyValue<StringName, FoundryScript::MemberInfo> &E : p_script->member_indices) {
				_rebind_self_data_type(E.value.data_type, p_script);
				_specialize_type_argument_binding(E.value.type_argument_binding, base_specialization, p_script);
			}

			// Re-specialize the base's per-ancestor type-parameter table one level through this class's
			// `extends Base[args]` as well, so `create_proxy[T]` (compiled once in an ancestor) can
			// resolve that ancestor's `T` for a derived instance whose base was specialized.
			p_script->type_parameter_bindings_by_ancestor = base->type_parameter_bindings_by_ancestor;
			for (KeyValue<FoundryScript *, Vector<FoundryScript::TypeArgumentBinding>> &ancestor_entry : p_script->type_parameter_bindings_by_ancestor) {
				for (FoundryScript::TypeArgumentBinding &binding : ancestor_entry.value) {
					_specialize_type_argument_binding(binding, base_specialization, p_script);
				}
			}
		} break;
		default: {
			_set_error("Parser bug (please report): invalid inheritance.", nullptr);
			return ERR_BUG;
		} break;
	}

	// Duplicate RPC information from base FoundryScript
	// Base script isn't valid because it should not have been compiled yet, but the reference contains relevant info.
	if (base_type.kind == FSDataType::FOUNDRY_SCRIPT && p_script->base.is_valid()) {
		p_script->rpc_config = p_script->base->rpc_config.duplicate();
	}

	for (FSParser::ClassNode *trait : p_class->resolved_traits) {
		const StringName trait_name = fs_trait_identity_name(trait);
		if (trait_name != StringName() && !p_script->script_trait_list.has(trait_name)) {
			p_script->script_trait_list.push_back(trait_name);
		}
	}

	// Record abstract requirements inherited through `uses` so a dynamic proxy of
	// this type intercepts them instead of falling through to native dispatch.
	_collect_trait_abstract_requirements(p_class, p_script);

	// Record the class's declared generic type parameters so they survive to runtime reflection.
	p_script->type_parameters.clear();
	for (int i = 0; i < p_class->type_parameters.size(); i++) {
		const FSParser::TypeParameterNode *parameter = p_class->type_parameters[i];
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		FoundryScript::TypeParameter type_parameter;
		type_parameter.name = parameter->identifier->name;
		type_parameter.index = i;
		if (parameter->bound != nullptr && !parameter->resolved_bound.has_no_type()) {
			type_parameter.has_bound = true;
			type_parameter.bound = parameter->resolved_bound.to_property_info(String());
		}
		p_script->type_parameters.push_back(type_parameter);
	}

	// This class's own type parameters resolve directly against its instances' reified arguments
	// (identity OPEN bindings), completing the per-ancestor table so `create_proxy[T]` works both on a
	// directly-specialized instance (`Mock[Greeter]`) and through an inherited specialization.
	if (!p_class->type_parameters.is_empty()) {
		Vector<FoundryScript::TypeArgumentBinding> own_bindings;
		own_bindings.resize(p_class->type_parameters.size());
		for (int i = 0; i < own_bindings.size(); i++) {
			own_bindings.write[i].kind = FoundryScript::TypeArgumentBinding::OPEN;
			own_bindings.write[i].leaf_ordinal = i;
		}
		p_script->type_parameter_bindings_by_ancestor[p_script] = own_bindings;
	}

	for (FSParser::ClassNode *trait : p_class->resolved_traits) {
		if (trait == nullptr || trait->type_parameters.is_empty()) {
			continue;
		}

		FSDataType trait_type = _gdtype_from_datatype(trait->get_datatype(), p_script, false);
		FoundryScript *trait_script = Object::cast_to<FoundryScript>(trait_type.script_type);
		if (trait_script == nullptr) {
			continue;
		}

		const HashMap<StringName, FSParser::DataType> substitutions = _trait_type_argument_substitution(p_class, trait);
		Vector<FoundryScript::TypeArgumentBinding> trait_bindings;
		trait_bindings.resize(trait->type_parameters.size());
		for (int i = 0; i < trait->type_parameters.size(); i++) {
			const FSParser::TypeParameterNode *type_parameter = trait->type_parameters[i];
			if (type_parameter == nullptr || type_parameter->identifier == nullptr) {
				continue;
			}
			const FSParser::DataType *argument = substitutions.getptr(type_parameter->identifier->name);
			if (argument == nullptr) {
				continue;
			}

			FoundryScript::TypeArgumentBinding &binding = trait_bindings.write[i];
			if (argument->kind == FSParser::DataType::TYPE_PARAMETER &&
					argument->type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS) {
				binding.kind = FoundryScript::TypeArgumentBinding::OPEN;
				binding.leaf_ordinal = argument->type_parameter_index;
			} else {
				binding.kind = FoundryScript::TypeArgumentBinding::FIXED;
				binding.fixed = _gdtype_from_datatype(*argument, p_script, false);
				binding.fixed_is_dependent = _datatype_contains_erased_type_parameter(*argument);
				binding.leaf_ordinal = -1;
			}
		}
		p_script->type_parameter_bindings_by_ancestor[trait_script] = trait_bindings;
	}

	// Flatten the applied traits' members into this script alongside the class's own
	// members so their state, constants, signals, and methods are recompiled per
	// implementer. Shadowed names and abstract requirements are filtered out here.
	Vector<const FSParser::ClassNode::Member *> members_to_compile;
	for (int i = 0; i < p_class->members.size(); i++) {
		members_to_compile.push_back(&p_class->members[i]);
	}
	_collect_flattened_trait_members(p_class, members_to_compile);

	if (p_class->is_enum_file && p_class->enum_file_decl != nullptr && p_class->enum_file_decl->identifier != nullptr) {
		const FSParser::EnumNode *enum_n = p_class->enum_file_decl;
		for (const FSParser::EnumNode::Value &enum_value : enum_n->values) {
			p_script->constants.insert(enum_value.identifier->name, enum_value.value);
		}
		p_script->constants.insert(enum_n->identifier->name, enum_n->dictionary);
	}

	for (int i = 0; i < members_to_compile.size(); i++) {
		const FSParser::ClassNode::Member &member = *members_to_compile[i];
		switch (member.type) {
			case FSParser::ClassNode::Member::VARIABLE: {
				const FSParser::VariableNode *variable = member.variable;
				StringName name = variable->identifier->name;

				FoundryScript::MemberInfo minfo;
				switch (variable->property) {
					case FSParser::VariableNode::PROP_NONE:
						break; // Nothing to do.
					case FSParser::VariableNode::PROP_SETGET:
						if (variable->setter_pointer != nullptr) {
							minfo.setter = variable->setter_pointer->name;
						}
						if (variable->getter_pointer != nullptr) {
							minfo.getter = variable->getter_pointer->name;
						}
						break;
					case FSParser::VariableNode::PROP_INLINE:
						if (variable->setter != nullptr) {
							minfo.setter = "@" + variable->identifier->name + "_setter";
						}
						if (variable->getter != nullptr) {
							minfo.getter = "@" + variable->identifier->name + "_getter";
						}
						break;
				}
				const FSParser::DataType member_datatype = _substitute_self_type_parameter_for_class(variable->get_datatype(), p_class);
				minfo.data_type = _gdtype_from_datatype(member_datatype, p_script);

				if (member_datatype.is_set() && member_datatype.is_hard_type() &&
						member_datatype.kind == FSParser::DataType::TYPE_PARAMETER &&
						member_datatype.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS) {
					// The slot stays an erased Variant (see `_gdtype_from_datatype`), but record that it stands
					// for one of this class's own type parameters so writes can validate against the
					// instance's reified argument at runtime (e.g. rejecting `box.value = "x"` on a `Box[int]`).
					// Open at this declaring level; a subclass that fixes it via `extends` re-resolves the
					// binding when it inherits the member.
					minfo.type_argument_binding.kind = FoundryScript::TypeArgumentBinding::OPEN;
					minfo.type_argument_binding.is_type_handle = member_datatype.is_type_handle_annotation;
					minfo.type_argument_binding.leaf_ordinal = member_datatype.type_parameter_index;
				}

				PropertyInfo prop_info = member_datatype.to_property_info(name);
				PropertyInfo export_info = variable->export_info;

				if (variable->exported) {
					if (!minfo.data_type.has_type()) {
						prop_info.type = export_info.type;
						prop_info.class_name = export_info.class_name;
					}
					prop_info.hint = export_info.hint;
					prop_info.hint_string = export_info.hint_string;
					prop_info.usage = export_info.usage;
				}
				prop_info.usage |= PROPERTY_USAGE_SCRIPT_VARIABLE;
				minfo.property_info = prop_info;

				if (variable->is_static) {
					minfo.index = p_script->static_variables_indices.size();
					p_script->static_variables_indices[name] = minfo;
				} else {
					minfo.index = p_script->member_indices.size();
					p_script->member_indices[name] = minfo;
					p_script->members.insert(name);
				}

				// Persist member-variable annotation metadata, including concrete trait-flattened
				// variables collected above. Keyed by name so the implementer's effective view wins.
				{
					Vector<FoundryScript::AnnotationUsage> variable_usages;
					_collect_annotations(variable->annotations, variable_usages);
					if (!variable_usages.is_empty()) {
						p_script->variable_annotations[name] = variable_usages;
					}
				}

#ifdef TOOLS_ENABLED
				if (variable->initializer != nullptr && variable->initializer->is_constant) {
					p_script->member_default_values[name] = variable->initializer->reduced_value;
					FSCompiler::convert_to_initializer_type(p_script->member_default_values[name], variable);
				} else {
					p_script->member_default_values.erase(name);
				}
#endif
			} break;

			case FSParser::ClassNode::Member::CONSTANT: {
				const FSParser::ConstantNode *constant = member.constant;
				StringName name = constant->identifier->name;

				p_script->constants.insert(name,
						_resolve_aliased_class_constant(constant->initializer->reduced_value,
								_constant_storage_datatype(constant), p_script));

				// Persist constant annotation metadata, keyed by name.
				Vector<FoundryScript::AnnotationUsage> constant_usages;
				_collect_annotations(constant->annotations, constant_usages);
				if (!constant_usages.is_empty()) {
					p_script->constant_annotations[name] = constant_usages;
				}
			} break;

			case FSParser::ClassNode::Member::ENUM_VALUE: {
				const FSParser::EnumNode::Value &enum_value = member.enum_value;
				StringName name = enum_value.identifier->name;

				p_script->constants.insert(name, enum_value.value);
			} break;

			case FSParser::ClassNode::Member::SIGNAL: {
				const FSParser::SignalNode *signal = member.signal;
				StringName name = signal->identifier->name;

				p_script->_signals[name] = signal->method_info;

				// Persist signal annotation metadata, keyed by name.
				Vector<FoundryScript::AnnotationUsage> signal_usages;
				_collect_annotations(signal->annotations, signal_usages);
				if (!signal_usages.is_empty()) {
					p_script->signal_annotations[name] = signal_usages;
				}
			} break;

			case FSParser::ClassNode::Member::ENUM: {
				const FSParser::EnumNode *enum_n = member.m_enum;
				StringName name = enum_n->identifier->name;

				p_script->constants.insert(name, enum_n->dictionary);
			} break;

			case FSParser::ClassNode::Member::GROUP: {
				const FSParser::AnnotationNode *annotation = member.annotation;
				// Avoid name conflict. See GH-78252.
				StringName name = vformat("@group_%d_%s", p_script->members.size(), annotation->export_info.name);

				// This is not a normal member, but we need this to keep indices in order.
				FoundryScript::MemberInfo minfo;
				minfo.index = p_script->member_indices.size();

				PropertyInfo prop_info;
				prop_info.name = annotation->export_info.name;
				prop_info.usage = annotation->export_info.usage;
				prop_info.hint_string = annotation->export_info.hint_string;
				minfo.property_info = prop_info;

				p_script->member_indices[name] = minfo;
				p_script->members.insert(name);
			} break;

			case FSParser::ClassNode::Member::FUNCTION: {
				const FSParser::FunctionNode *function_n = member.function;

				Variant config = function_n->rpc_config;
				if (config.get_type() != Variant::NIL) {
					p_script->rpc_config[function_n->identifier->name] = config;
				}
			} break;
			default:
				break; // Nothing to do here.
		}
	}

	p_script->static_variables.resize(p_script->static_variables_indices.size());

	// Finalize the slot-indexed type-argument bindings (parallel to instance `members`) so a direct
	// member-store opcode can resolve a `T`-typed member from the leaf script by slot.
	p_script->member_type_argument_bindings.resize(p_script->member_indices.size());
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &E : p_script->member_indices) {
		if (E.value.index >= 0 && E.value.index < p_script->member_type_argument_bindings.size()) {
			p_script->member_type_argument_bindings.write[E.value.index] = E.value.type_argument_binding;
		}
	}

	parsed_classes.insert(p_script);
	parsing_classes.erase(p_script);

	// Populate inner classes.
	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type != member.CLASS) {
			continue;
		}
		const FSParser::ClassNode *inner_class = member.m_class;
		StringName name = inner_class->identifier->name;
		Ref<FoundryScript> &subclass = p_script->subclasses[name];
		FoundryScript *subclass_ptr = subclass.ptr();

		// Subclass might still be parsing, just skip it
		if (!parsing_classes.has(subclass_ptr)) {
			Error err = _prepare_compilation(subclass_ptr, inner_class, p_keep_state);
			if (err) {
				return err;
			}
		}

		p_script->constants.insert(name, subclass); //once parsed, goes to the list of constants
	}

	return OK;
}

Error FSCompiler::_compile_class(FoundryScript *p_script, const FSParser::ClassNode *p_class, bool p_keep_state) {
	// Compile member functions, getters, and setters, including the bodies flattened in
	// from applied traits. Trait functions are compiled against the implementing script
	// so member accesses bind to the flattened member layout of this class.
	Vector<const FSParser::ClassNode::Member *> members_to_compile;
	for (int i = 0; i < p_class->members.size(); i++) {
		members_to_compile.push_back(&p_class->members[i]);
	}
	_collect_flattened_trait_members(p_class, members_to_compile);

	for (int i = 0; i < members_to_compile.size(); i++) {
		const FSParser::ClassNode::Member &member = *members_to_compile[i];
		if (member.type == member.FUNCTION) {
			const FSParser::FunctionNode *function = member.function;
			Error err = OK;
			_parse_function(err, p_script, p_class, function);
			if (err) {
				return err;
			}

			// Persist method annotation metadata, including concrete trait-flattened methods
			// collected above. Keyed by name so an overriding method's annotations are effective.
			Vector<FoundryScript::AnnotationUsage> method_usages;
			_collect_annotations(function->annotations, method_usages);
			if (!method_usages.is_empty()) {
				p_script->method_annotations[function->identifier->name] = method_usages;
			}
		} else if (member.type == member.VARIABLE) {
			const FSParser::VariableNode *variable = member.variable;
			if (variable->property == FSParser::VariableNode::PROP_INLINE) {
				if (variable->setter != nullptr) {
					Error err = _parse_setter_getter(p_script, p_class, variable, true);
					if (err) {
						return err;
					}
				}
				if (variable->getter != nullptr) {
					Error err = _parse_setter_getter(p_script, p_class, variable, false);
					if (err) {
						return err;
					}
				}
			}
		}
	}

	{
		// Create `@implicit_new()` special function in any case.
		Error err = OK;
		_parse_function(err, p_script, p_class, nullptr);
		if (err) {
			return err;
		}
	}

	// `@onready` variables flattened in from applied traits also require the
	// `@implicit_ready()` function, even when the implementer declares none of its own.
	bool onready_used = p_class->onready_used;
	if (!onready_used) {
		Vector<const FSParser::ClassNode::Member *> flattened_trait_members;
		_collect_flattened_trait_members(p_class, flattened_trait_members);
		for (const FSParser::ClassNode::Member *member_ptr : flattened_trait_members) {
			if (member_ptr->type == FSParser::ClassNode::Member::VARIABLE && member_ptr->variable->onready) {
				onready_used = true;
				break;
			}
		}
	}

	if (onready_used) {
		// Create `@implicit_ready()` special function.
		Error err = OK;
		_parse_function(err, p_script, p_class, nullptr, true);
		if (err) {
			return err;
		}
	}

	if (_class_or_traits_have_static_data(p_class)) {
		Error err = OK;
		FSFunction *func = _make_static_initializer(err, p_script, p_class);
		p_script->static_initializer = func;
		if (err) {
			return err;
		}
	}

#ifdef DEBUG_ENABLED

	//validate instances if keeping state

	if (p_keep_state) {
		for (RBSet<Object *>::Element *E = p_script->instances.front(); E;) {
			RBSet<Object *>::Element *N = E->next();

			ScriptInstance *si = E->get()->get_script_instance();
			if (si->is_placeholder()) {
#ifdef TOOLS_ENABLED
				PlaceHolderScriptInstance *psi = static_cast<PlaceHolderScriptInstance *>(si);

				if (p_script->is_tool()) {
					//re-create as an instance
					p_script->placeholders.erase(psi); //remove placeholder

					FSInstance *instance = memnew(FSInstance);
					instance->members.resize(p_script->member_indices.size());
					instance->script = Ref<FoundryScript>(p_script);
					instance->owner = E->get();

					//needed for hot reloading
					for (const KeyValue<StringName, FoundryScript::MemberInfo> &F : p_script->member_indices) {
						instance->member_indices_cache[F.key] = F.value.index;
					}
					instance->owner->set_script_instance(instance);

					/* STEP 2, INITIALIZE AND CONSTRUCT */

					Callable::CallError ce;
					p_script->initializer->call(instance, nullptr, 0, ce);

					if (ce.error != Callable::CallError::CALL_OK) {
						//well, tough luck, not gonna do anything here
					}
				}
#endif // TOOLS_ENABLED
			} else {
				FSInstance *gi = static_cast<FSInstance *>(si);
				gi->reload_members();
			}

			E = N;
		}
	}
#endif //DEBUG_ENABLED

	// Trait static data is flattened into this script, so it must drive `has_static_data`
	// too — otherwise a class with only trait-provided static data would skip static-script
	// registration and its flattened static state would not be pinned by the static cache.
	has_static_data = _class_or_traits_have_static_data(p_class);

	for (int i = 0; i < p_class->members.size(); i++) {
		if (p_class->members[i].type != FSParser::ClassNode::Member::CLASS) {
			continue;
		}
		const FSParser::ClassNode *inner_class = p_class->members[i].m_class;
		StringName name = inner_class->identifier->name;
		FoundryScript *subclass = p_script->subclasses[name].ptr();

		Error err = _compile_class(subclass, inner_class, p_keep_state);
		if (err) {
			return err;
		}

		has_static_data = has_static_data || _class_or_traits_have_static_data(inner_class);
	}

	p_script->_static_default_init();

	p_script->valid = true;
	return OK;
}

void FSCompiler::convert_to_initializer_type(Variant &p_variant, const FSParser::VariableNode *p_node) {
	// Set p_variant to the value of p_node's initializer, with the type of p_node's variable.
	FSParser::DataType member_t = p_node->datatype;
	FSParser::DataType init_t = p_node->initializer->datatype;
	if (member_t.is_hard_type() && init_t.is_hard_type() &&
			member_t.kind == FSParser::DataType::BUILTIN && init_t.kind == FSParser::DataType::BUILTIN) {
		if (Variant::can_convert_strict(init_t.builtin_type, member_t.builtin_type)) {
			const Variant *v = &p_node->initializer->reduced_value;
			Callable::CallError ce;
			Variant::construct(member_t.builtin_type, p_variant, &v, 1, ce);
		}
	}
}

void FSCompiler::make_scripts(FoundryScript *p_script, const FSParser::ClassNode *p_class, bool p_keep_state) {
	p_script->fully_qualified_name = p_class->fqcn;
	p_script->local_name = p_class->identifier ? p_class->identifier->name : StringName();
	p_script->global_name = p_class->get_global_name();
	p_script->simplified_icon_path = p_class->simplified_icon_path;

	HashMap<StringName, Ref<FoundryScript>> old_subclasses;

	if (p_keep_state) {
		old_subclasses = p_script->subclasses;
	}

	p_script->subclasses.clear();

	for (int i = 0; i < p_class->members.size(); i++) {
		if (p_class->members[i].type != FSParser::ClassNode::Member::CLASS) {
			continue;
		}
		const FSParser::ClassNode *inner_class = p_class->members[i].m_class;
		StringName name = inner_class->identifier->name;

		Ref<FoundryScript> subclass;

		if (old_subclasses.has(name)) {
			subclass = old_subclasses[name];
		} else {
			subclass = FSLanguage::get_singleton()->get_orphan_subclass(inner_class->fqcn);
		}

		if (subclass.is_null()) {
			subclass.instantiate();
		}

		subclass->_owner = p_script;
		subclass->path = p_script->path;
		p_script->subclasses.insert(name, subclass);

		make_scripts(subclass.ptr(), inner_class, p_keep_state);
	}
}

FSCompiler::FunctionLambdaInfo FSCompiler::_get_function_replacement_info(FSFunction *p_func, int p_index, int p_depth, FSFunction *p_parent_func) {
	FunctionLambdaInfo info;
	info.function = p_func;
	info.parent = p_parent_func;
	info.script = p_func->get_script();
	info.name = p_func->get_name();
	info.line = p_func->_initial_line;
	info.index = p_index;
	info.depth = p_depth;
	info.capture_count = 0;
	info.use_self = false;
	info.arg_count = p_func->_argument_count;
	info.default_arg_count = p_func->_default_arg_count;
	info.sublambdas = _get_function_lambda_replacement_info(p_func, p_depth, p_parent_func);

	ERR_FAIL_NULL_V(info.script, info);
	FoundryScript::LambdaInfo *extra_info = info.script->lambda_info.getptr(p_func);
	if (extra_info != nullptr) {
		info.capture_count = extra_info->capture_count;
		info.use_self = extra_info->use_self;
	} else {
		info.capture_count = 0;
		info.use_self = false;
	}

	return info;
}

Vector<FSCompiler::FunctionLambdaInfo> FSCompiler::_get_function_lambda_replacement_info(FSFunction *p_func, int p_depth, FSFunction *p_parent_func) {
	Vector<FunctionLambdaInfo> result;
	// Only scrape the lambdas inside p_func.
	for (int i = 0; i < p_func->lambdas.size(); ++i) {
		result.push_back(_get_function_replacement_info(p_func->lambdas[i], i, p_depth + 1, p_func));
	}
	return result;
}

FSCompiler::ScriptLambdaInfo FSCompiler::_get_script_lambda_replacement_info(FoundryScript *p_script) {
	ScriptLambdaInfo info;

	if (p_script->implicit_initializer) {
		info.implicit_initializer_info = _get_function_lambda_replacement_info(p_script->implicit_initializer);
	}
	if (p_script->implicit_ready) {
		info.implicit_ready_info = _get_function_lambda_replacement_info(p_script->implicit_ready);
	}
	if (p_script->static_initializer) {
		info.static_initializer_info = _get_function_lambda_replacement_info(p_script->static_initializer);
	}

	for (const KeyValue<StringName, FSFunction *> &E : p_script->member_functions) {
		info.member_function_infos.insert(E.key, _get_function_lambda_replacement_info(E.value));
	}

	for (const KeyValue<StringName, Ref<FoundryScript>> &KV : p_script->get_subclasses()) {
		info.subclass_info.insert(KV.key, _get_script_lambda_replacement_info(KV.value.ptr()));
	}

	return info;
}

bool FSCompiler::_do_function_infos_match(const FunctionLambdaInfo &p_old_info, const FunctionLambdaInfo *p_new_info) {
	if (p_new_info == nullptr) {
		return false;
	}

	if (p_new_info->capture_count != p_old_info.capture_count || p_new_info->use_self != p_old_info.use_self) {
		return false;
	}

	int old_required_arg_count = p_old_info.arg_count - p_old_info.default_arg_count;
	int new_required_arg_count = p_new_info->arg_count - p_new_info->default_arg_count;
	if (new_required_arg_count > old_required_arg_count || p_new_info->arg_count < old_required_arg_count) {
		return false;
	}

	return true;
}

void FSCompiler::_get_function_ptr_replacements(HashMap<FSFunction *, FSFunction *> &r_replacements, const FunctionLambdaInfo &p_old_info, const FunctionLambdaInfo *p_new_info) {
	ERR_FAIL_COND(r_replacements.has(p_old_info.function));
	if (!_do_function_infos_match(p_old_info, p_new_info)) {
		p_new_info = nullptr;
	}

	r_replacements.insert(p_old_info.function, p_new_info != nullptr ? p_new_info->function : nullptr);
	_get_function_ptr_replacements(r_replacements, p_old_info.sublambdas, p_new_info != nullptr ? &p_new_info->sublambdas : nullptr);
}

void FSCompiler::_get_function_ptr_replacements(HashMap<FSFunction *, FSFunction *> &r_replacements, const Vector<FunctionLambdaInfo> &p_old_infos, const Vector<FunctionLambdaInfo> *p_new_infos) {
	for (int i = 0; i < p_old_infos.size(); ++i) {
		const FunctionLambdaInfo &old_info = p_old_infos[i];
		const FunctionLambdaInfo *new_info = nullptr;
		if (p_new_infos != nullptr && p_new_infos->size() == p_old_infos.size()) {
			// For now only attempt if the size is the same.
			new_info = &p_new_infos->get(i);
		}
		_get_function_ptr_replacements(r_replacements, old_info, new_info);
	}
}

void FSCompiler::_get_function_ptr_replacements(HashMap<FSFunction *, FSFunction *> &r_replacements, const ScriptLambdaInfo &p_old_info, const ScriptLambdaInfo *p_new_info) {
	_get_function_ptr_replacements(r_replacements, p_old_info.implicit_initializer_info, p_new_info != nullptr ? &p_new_info->implicit_initializer_info : nullptr);
	_get_function_ptr_replacements(r_replacements, p_old_info.implicit_ready_info, p_new_info != nullptr ? &p_new_info->implicit_ready_info : nullptr);
	_get_function_ptr_replacements(r_replacements, p_old_info.static_initializer_info, p_new_info != nullptr ? &p_new_info->static_initializer_info : nullptr);

	for (const KeyValue<StringName, Vector<FunctionLambdaInfo>> &old_kv : p_old_info.member_function_infos) {
		_get_function_ptr_replacements(r_replacements, old_kv.value, p_new_info != nullptr ? p_new_info->member_function_infos.getptr(old_kv.key) : nullptr);
	}
	for (int i = 0; i < p_old_info.other_function_infos.size(); ++i) {
		const FunctionLambdaInfo &old_other_info = p_old_info.other_function_infos[i];
		const FunctionLambdaInfo *new_other_info = nullptr;
		if (p_new_info != nullptr && p_new_info->other_function_infos.size() == p_old_info.other_function_infos.size()) {
			// For now only attempt if the size is the same.
			new_other_info = &p_new_info->other_function_infos[i];
		}
		// Needs to be called on all old lambdas, even if there's no replacement.
		_get_function_ptr_replacements(r_replacements, old_other_info, new_other_info);
	}
	for (const KeyValue<StringName, ScriptLambdaInfo> &old_kv : p_old_info.subclass_info) {
		const ScriptLambdaInfo &old_subinfo = old_kv.value;
		const ScriptLambdaInfo *new_subinfo = p_new_info != nullptr ? p_new_info->subclass_info.getptr(old_kv.key) : nullptr;
		_get_function_ptr_replacements(r_replacements, old_subinfo, new_subinfo);
	}
}

Error FSCompiler::compile(const FSParser *p_parser, FoundryScript *p_script, bool p_keep_state) {
	err_line = -1;
	err_column = -1;
	error = "";
	parser = p_parser;
	main_script = p_script;
	const FSParser::ClassNode *root = parser->get_tree();

	source = p_script->get_path();

	ScriptLambdaInfo old_lambda_info = _get_script_lambda_replacement_info(p_script);

	// Create scripts for subclasses beforehand so they can be referenced
	make_scripts(p_script, root, p_keep_state);

	main_script->_owner = nullptr;
	Error err = _prepare_compilation(main_script, parser->get_tree(), p_keep_state);

	if (err) {
		return err;
	}

	err = _compile_class(main_script, root, p_keep_state);
	if (err) {
		return err;
	}

	ScriptLambdaInfo new_lambda_info = _get_script_lambda_replacement_info(p_script);

	HashMap<FSFunction *, FSFunction *> func_ptr_replacements;
	_get_function_ptr_replacements(func_ptr_replacements, old_lambda_info, &new_lambda_info);
	main_script->_recurse_replace_function_ptrs(func_ptr_replacements);

	if (has_static_data && !root->annotated_static_unload) {
		FSCache::add_static_script(p_script);
	}

	err = FSCache::finish_compiling(main_script->path);
	if (err) {
		_set_error(R"(Failed to compile depended scripts.)", nullptr);
	}
	return err;
}

String FSCompiler::get_error() const {
	return error;
}

int FSCompiler::get_error_line() const {
	return err_line;
}

int FSCompiler::get_error_column() const {
	return err_column;
}

FSCompiler::FSCompiler() {
}
