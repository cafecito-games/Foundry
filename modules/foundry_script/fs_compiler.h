/**************************************************************************/
/*  fs_compiler.h                                                         */
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

#include "foundry_script.h"
#include "fs_codegen.h"
#include "fs_function.h"
#include "fs_parser.h"

#include "core/templates/hash_set.h"

class FSCompiler {
	const FSParser *parser = nullptr;
	HashSet<FoundryScript *> parsed_classes;
	HashSet<FoundryScript *> parsing_classes;
	FoundryScript *main_script = nullptr;
	// Set only while conformance witnesses are compiled, so a witness (and any lambda inside it) can
	// still reach the constant pool of the file that declares the `extend`.
	FoundryScript *witness_declaration_site_script = nullptr;
	// Set only while a conformance witness (and any lambda inside it) is compiled. Inside a witness,
	// `Self` denotes the conformance target, exactly as it does during analysis; the script that owns
	// the generated function is only the bytecode/storage context, and for a builtin or native target
	// it is the declaring file. Unset outside witness compilation, where `Self` keeps lowering against
	// the owning class.
	FSParser::DataType witness_self_type;

	struct FunctionLambdaInfo {
		FSFunction *function = nullptr;
		FSFunction *parent = nullptr;
		FoundryScript *script = nullptr;
		StringName name;
		int line = 0;
		int index = 0;
		int depth = 0;
		//uint64_t code_hash;
		//int code_size;
		int capture_count = 0;
		bool use_self = false;
		int arg_count = 0;
		int default_arg_count = 0;
		//Vector<FSDataType> argument_types;
		//FSDataType return_type;
		Vector<FunctionLambdaInfo> sublambdas;
	};

	struct ScriptLambdaInfo {
		struct EnumFunctionLambdaInfo {
			HashMap<StringName, Vector<FunctionLambdaInfo>> instance_function_infos;
			HashMap<StringName, Vector<FunctionLambdaInfo>> static_function_infos;
		};

		Vector<FunctionLambdaInfo> implicit_initializer_info;
		Vector<FunctionLambdaInfo> implicit_ready_info;
		Vector<FunctionLambdaInfo> static_initializer_info;
		HashMap<StringName, Vector<FunctionLambdaInfo>> member_function_infos;
		HashMap<StringName, EnumFunctionLambdaInfo> enum_function_infos;
		Vector<FunctionLambdaInfo> other_function_infos;
		HashMap<StringName, ScriptLambdaInfo> subclass_info;
	};

	struct CodeGen {
		FoundryScript *script = nullptr;
		// While a conformance witness is compiled against a foreign target, `script` is the target's
		// script; this is the script of the file that declares the `extend`, whose constant pool backs
		// the declaration-site half of the witness's scope.
		FoundryScript *declaration_site_script = nullptr;
		const FSParser::ClassNode *class_node = nullptr;
		const FSParser::FunctionNode *function_node = nullptr;
		StringName function_name;
		FSCodeGenerator *generator = nullptr;
		HashMap<StringName, FSCodeGenerator::Address> parameters;
		HashMap<StringName, FSCodeGenerator::Address> locals;
		List<HashMap<StringName, FSCodeGenerator::Address>> locals_stack;
		bool is_static = false;

		FSCodeGenerator::Address add_local(const StringName &p_name, const FSDataType &p_type) {
			uint32_t addr = generator->add_local(p_name, p_type);
			locals[p_name] = FSCodeGenerator::Address(FSCodeGenerator::Address::LOCAL_VARIABLE, addr, p_type);
			return locals[p_name];
		}

		FSCodeGenerator::Address add_local_constant(const StringName &p_name, const Variant &p_value) {
			uint32_t addr = generator->add_local_constant(p_name, p_value);
			locals[p_name] = FSCodeGenerator::Address(FSCodeGenerator::Address::CONSTANT, addr);
			return locals[p_name];
		}

		FSCodeGenerator::Address add_temporary(const FSDataType &p_type = FSDataType()) {
			uint32_t addr = generator->add_temporary(p_type);
			return FSCodeGenerator::Address(FSCodeGenerator::Address::TEMPORARY, addr, p_type);
		}

		FSCodeGenerator::Address add_constant(const Variant &p_constant) {
			FSDataType type;
			type.kind = FSDataType::BUILTIN;
			type.builtin_type = p_constant.get_type();
			if (type.builtin_type == Variant::OBJECT) {
				Object *obj = p_constant;
				if (obj) {
					type.kind = FSDataType::NATIVE;
					type.native_type = obj->get_class_name();
					FSSpecializedClassHandle *specialized_handle = Object::cast_to<FSSpecializedClassHandle>(obj);
					if (specialized_handle != nullptr && specialized_handle->get_specialized_script().is_valid()) {
						ContainerType handle_type;
						handle_type.builtin_type = Variant::OBJECT;
						handle_type.class_name = specialized_handle->get_specialized_script()->get_instance_base_type();
						handle_type.script = specialized_handle->get_specialized_script();
						handle_type.type_arguments = specialized_handle->get_type_arguments();
						type = FSDataType::from_type_handle_container_type(handle_type);
					}

					Ref<Script> scr = obj->get_script();
					if (scr.is_valid()) {
						type.script_type = scr.ptr();
						Ref<FoundryScript> foundry_script = scr;
						if (foundry_script.is_valid()) {
							type.kind = FSDataType::FOUNDRY_SCRIPT;
						} else {
							type.kind = FSDataType::SCRIPT;
						}
					}
				} else {
					type.builtin_type = Variant::NIL;
				}
			}

			uint32_t addr = generator->add_or_get_constant(p_constant);
			return FSCodeGenerator::Address(FSCodeGenerator::Address::CONSTANT, addr, type);
		}

		void start_block() {
			HashMap<StringName, FSCodeGenerator::Address> old_locals = locals;
			locals_stack.push_back(old_locals);
			generator->start_block();
		}

		void end_block() {
			locals = locals_stack.back()->get();
			locals_stack.pop_back();
			generator->end_block();
		}
	};

	bool _is_class_member_property(CodeGen &codegen, const StringName &p_name);
	bool _is_class_member_property(FoundryScript *owner, const StringName &p_name);
	bool _is_local_or_parameter(CodeGen &codegen, const StringName &p_name);

	void _set_error(const String &p_error, const FSParser::Node *p_node);

	FSDataType _gdtype_from_datatype(const FSParser::DataType &p_datatype, FoundryScript *p_owner, bool p_handle_metatype = true);
	FSDataType _gdtype_tuple_test_type_from_datatype(const FSParser::DataType &p_datatype, FoundryScript *p_owner);
	// A `const` aliasing a class in this compilation unit (`const Alias = Box`) folds to the analyzer's
	// shallow, uncompiled class object; constructing through it (`Alias.new()`) fails. Re-point such a
	// folded value at the live subclass compiled in this unit so the alias matches the inner-class name.
	Variant _resolve_aliased_class_constant(const Variant &p_value);
	Variant _resolve_aliased_class_constant(const Variant &p_value, const FSParser::DataType &p_datatype,
			FoundryScript *p_owner);
	// Re-resolve a still-open type-argument binding one level through a subclass's `extends Base[args]`
	// specialization: a forwarded class parameter stays OPEN (remapped ordinal), a concrete argument
	// becomes FIXED. Used when a subclass inherits a base's member and per-ancestor parameter bindings.
	void _specialize_type_argument_binding(FoundryScript::TypeArgumentBinding &r_binding, const Vector<FSParser::DataType> &p_base_specialization, FoundryScript *p_owner);

	// Resolves a class constant / nested-class handle exactly the way the analyzer's scope walk does:
	// the script itself, then its complete base subtree (each base contributing its own lexical outer
	// chain), then the script's own lexical outer chain. Visiting an inherited inner class's outer
	// scope before the current class's outer scope is what keeps the emitted declaration identical to
	// the one analysis chose. Scripts are pointer-deduplicated so a shared outer is walked once.
	bool _find_class_scope_constant(FoundryScript *p_script, const StringName &p_name, Variant &r_value,
			HashSet<FoundryScript *> &r_visited);
	// The live class a resolved class datatype denotes, or null when it cannot be recovered. Emitting
	// this identity is preferred over a second name lookup: the declaration analysis chose can live in
	// a script this compilation unit only holds shallowly, whose constant pool is not populated.
	FoundryScript *_resolve_class_handle_script(const FSParser::DataType &p_datatype, FoundryScript *p_owner);

	FSCodeGenerator::Address _emit_global_class_value(CodeGen &codegen, Error &r_error, const StringName &p_global_class, const FSParser::ExpressionNode *p_source);
	FSCodeGenerator::Address _parse_expression(CodeGen &codegen, Error &r_error, const FSParser::ExpressionNode *p_expression, bool p_root = false, bool p_initializer = false);
	FSCodeGenerator::Address _parse_match_pattern(CodeGen &codegen, Error &r_error, const FSParser::PatternNode *p_pattern, const FSCodeGenerator::Address &p_value_addr, const FSCodeGenerator::Address &p_type_addr, const FSCodeGenerator::Address &p_previous_test, bool p_is_first, bool p_is_nested);
	// Lowers `is` against an enum type: a membership test, or a tagged-union case test that also binds
	// the case payload.
	void _parse_enum_type_test(CodeGen &codegen, const FSParser::TypeTestNode *p_type_test, const FSCodeGenerator::Address &p_target, const FSCodeGenerator::Address &p_source);
	// Allocates a block's locals. When r_case_bind_locals is given, `is Case(...)` binds are collected
	// there instead of in the returned list, so the caller can clear the rest without wiping a bind the
	// guarded suite is about to read.
	List<FSCodeGenerator::Address> _add_block_locals(CodeGen &codegen, const FSParser::SuiteNode *p_block, List<FSCodeGenerator::Address> *r_case_bind_locals = nullptr);
	void _clear_block_locals(CodeGen &codegen, const List<FSCodeGenerator::Address> &p_locals);
	Error _parse_block(CodeGen &codegen, const FSParser::SuiteNode *p_block, bool p_add_locals = true, bool p_clear_locals = true);
	static void _collect_flattened_trait_members(const FSParser::ClassNode *p_class, Vector<const FSParser::ClassNode::Member *> &r_members);
	static void _collect_annotations(const List<FSParser::AnnotationNode *> &p_annotations, Vector<FoundryScript::AnnotationUsage> &r_usages);
	static void _merge_annotation_usages(HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &r_annotation_map,
			const StringName &p_name, const Vector<FoundryScript::AnnotationUsage> &p_usages);
	static void _collect_parameter_annotations(const Vector<FSParser::ParameterNode *> &p_parameters, const FSParser::ParameterNode *p_rest_parameter, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &r_parameter_annotations);
	void _collect_trait_abstract_requirements(const FSParser::ClassNode *p_class, FoundryScript *p_script);
	FSFunction *_parse_function(Error &r_error, FoundryScript *p_script, const FSParser::ClassNode *p_class, const FSParser::FunctionNode *p_func, bool p_for_ready = false, bool p_for_lambda = false, bool p_skip_member_register = false);
	Error _compile_enum_functions(
			FoundryScript *p_script, const FSParser::ClassNode *p_class, const FSParser::EnumNode *p_enum);
	Error _compile_conformance_witnesses(FoundryScript *p_script, const FSParser::ClassNode *p_class);
	Error _load_namespace_conformance_scripts(FoundryScript *p_script);
	void _invalidate_compiled_classes(FoundryScript *p_script);
	void _withdraw_runtime_witnesses(FoundryScript *p_script);
	FSFunction *_make_static_initializer(Error &r_error, FoundryScript *p_script, const FSParser::ClassNode *p_class);
	Error _parse_setter_getter(FoundryScript *p_script, const FSParser::ClassNode *p_class, const FSParser::VariableNode *p_variable, bool p_is_setter);
	Error _prepare_compilation(FoundryScript *p_script, const FSParser::ClassNode *p_class, bool p_keep_state);
	Error _compile_class(FoundryScript *p_script, const FSParser::ClassNode *p_class, bool p_keep_state);
	FunctionLambdaInfo _get_function_replacement_info(FSFunction *p_func, int p_index = -1, int p_depth = 0, FSFunction *p_parent_func = nullptr);
	Vector<FunctionLambdaInfo> _get_function_lambda_replacement_info(FSFunction *p_func, int p_depth = 0, FSFunction *p_parent_func = nullptr);
	ScriptLambdaInfo _get_script_lambda_replacement_info(FoundryScript *p_script);
	bool _do_function_infos_match(const FunctionLambdaInfo &p_old_info, const FunctionLambdaInfo *p_new_info);
	void _get_function_ptr_replacements(HashMap<FSFunction *, FSFunction *> &r_replacements, const FunctionLambdaInfo &p_old_info, const FunctionLambdaInfo *p_new_info);
	void _get_function_ptr_replacements(HashMap<FSFunction *, FSFunction *> &r_replacements, const Vector<FunctionLambdaInfo> &p_old_infos, const Vector<FunctionLambdaInfo> *p_new_infos);
	void _get_function_ptr_replacements(HashMap<FSFunction *, FSFunction *> &r_replacements, const ScriptLambdaInfo &p_old_info, const ScriptLambdaInfo *p_new_info);
	int err_line = 0;
	int err_column = 0;
	StringName source;
	String error;
	FSParser::ExpressionNode *awaited_node = nullptr;
	bool has_static_data = false;

public:
	static void convert_to_initializer_type(Variant &p_variant, const FSParser::VariableNode *p_node);
	static void collect_passive_annotations(const List<FSParser::AnnotationNode *> &p_annotations, Vector<FoundryScript::AnnotationUsage> &r_usages);
	static void collect_passive_parameter_annotations(const Vector<FSParser::ParameterNode *> &p_parameters, const FSParser::ParameterNode *p_rest_parameter, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &r_parameter_annotations);
	static void make_scripts(FoundryScript *p_script, const FSParser::ClassNode *p_class, bool p_keep_state);
	Error compile(const FSParser *p_parser, FoundryScript *p_script, bool p_keep_state = false);

	String get_error() const;
	int get_error_line() const;
	int get_error_column() const;

	FSCompiler();
};
