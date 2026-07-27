/**************************************************************************/
/*  fs_function.h                                                         */
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

#include "fs_utility_functions.h"

#include "core/object/ref_counted.h"
#include "core/object/script_function_state.h"
#include "core/object/script_language.h"
#include "core/os/thread.h"
#include "core/string/string_name.h"
#include "core/templates/pair.h"
#include "core/templates/self_list.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/variant.h"

class FSInstance;
class FoundryScript;
#ifdef TESTS_ENABLED
namespace FSTests {
class TestFSBytecodeScriptAccessor;
}
#endif // TESTS_ENABLED
#ifdef TOOLS_ENABLED
class FSNameManglerApplication;
class FSNameManglerAnalysis;
#endif

class FSDataType {
public:
	Vector<FSDataType> container_element_types;

	// NOTE: `TYPE_PARAMETER` must remain the last/highest value. The compiled-bytecode loader
	// (`FSBytecodeLoader::decode_data_type`) validates a deserialized kind with `kind > TYPE_PARAMETER`;
	// if a new kind is appended after it, update that upper-bound check to the new last value.
	enum Kind {
		VARIANT, // Can be any type.
		BUILTIN,
		NATIVE,
		SCRIPT,
		FOUNDRY_SCRIPT,
		TYPE_PARAMETER, // Generic type parameter, erased before execution.
	};

	Kind kind = VARIANT;

	// NOTE: `TYPE_PARAMETER_METHOD` must remain the last/highest value. The compiled-bytecode loader
	// validates a deserialized scope with `type_parameter_scope > TYPE_PARAMETER_METHOD`; update that
	// check if a new scope is appended after it.
	enum TypeParameterScope {
		TYPE_PARAMETER_NONE,
		TYPE_PARAMETER_CLASS,
		TYPE_PARAMETER_METHOD,
	};

	Variant::Type builtin_type = Variant::NIL;
	StringName native_type;
	Script *script_type = nullptr;
	Ref<Script> script_type_ref;
	// Whether this type also accepts null. Builtin and enum kinds reject null otherwise; object kinds
	// already do at runtime, but the flag keeps the information available for them too.
	bool is_nullable = false;
	bool is_type_handle = false;
	bool is_self_type = false;
	bool is_script_trait = false;
	StringName script_trait;

	// For TYPE_PARAMETER kind.
	StringName type_parameter_name;
	int type_parameter_index = -1;
	TypeParameterScope type_parameter_scope = TYPE_PARAMETER_NONE;

	// Type arguments of a specialized type handle, e.g. the `int` in `Box[int]`. Empty for unspecialized types.
	Vector<FSDataType> type_arguments;

	_FORCE_INLINE_ bool has_type() const { return kind != VARIANT; }

	bool is_type_handle_type(const Variant &p_variant) const;
	static FSDataType from_type_handle_container_type(const ContainerType &p_container_type);

	// True when `p_base` (or any of its base scripts) is retroactively conformed to trait `p_trait` via
	// an external `extend ... uses` declaration recorded in the conformance registry. Used as a runtime
	// fallback so a retroactively-conformed value satisfies a trait-typed slot even though the trait is
	// absent from the script's own trait list.
	static bool _script_conforms_to_trait(const Ref<Script> &p_base, const StringName &p_trait);

	// True when native engine class `p_native_class` (or any ancestor) is retroactively conformed to
	// trait `p_trait` via an external `extend <native class> uses` declaration. Lets a scriptless native
	// object satisfy a trait-typed slot at runtime.
	static bool _native_class_conforms_to_trait(const StringName &p_native_class, const StringName &p_trait);

	bool is_type(const Variant &p_variant, bool p_allow_implicit_conversion = false) const {
		if (is_nullable && p_variant.get_type() == Variant::NIL) {
			return true;
		}
		if (is_type_handle) {
			return is_type_handle_type(p_variant);
		}
		switch (kind) {
			case VARIANT: {
				return true;
			} break;
			case BUILTIN: {
				Variant::Type var_type = p_variant.get_type();
				bool valid = builtin_type == var_type;
				if (valid && builtin_type == Variant::ARRAY && has_container_element_type(0)) {
					Array array = p_variant;
					valid = array.is_typed() && container_element_types[0].is_same_container_type(array.get_element_type());
				} else if (valid && builtin_type == Variant::DICTIONARY && has_container_element_types()) {
					Dictionary dictionary = p_variant;
					if (dictionary.is_typed()) {
						FSDataType key = get_container_element_type_or_variant(0);
						FSDataType value = get_container_element_type_or_variant(1);
						valid = key.is_same_container_type(dictionary.get_key_type()) && value.is_same_container_type(dictionary.get_value_type());
					} else {
						valid = false;
					}
				} else if (!valid && p_allow_implicit_conversion) {
					valid = Variant::can_convert_strict(var_type, builtin_type);
				}
				return valid;
			} break;
			case NATIVE: {
				if (p_variant.get_type() == Variant::NIL) {
					return true;
				}
				if (p_variant.get_type() != Variant::OBJECT) {
					return false;
				}

				bool was_freed = false;
				Object *obj = p_variant.get_validated_object_with_check(was_freed);
				if (!obj) {
					return !was_freed;
				}

				if (!ClassDB::is_parent_class(obj->get_class_name(), native_type)) {
					return false;
				}
				return true;
			} break;
			case SCRIPT:
			case FOUNDRY_SCRIPT: {
				if (p_variant.get_type() == Variant::NIL) {
					return true;
				}
				if (p_variant.get_type() != Variant::OBJECT) {
					return false;
				}

				bool was_freed = false;
				Object *obj = p_variant.get_validated_object_with_check(was_freed);
				if (!obj) {
					return !was_freed;
				}

				Ref<Script> base = obj && obj->get_script_instance() ? obj->get_script_instance()->get_script() : nullptr;
				if (is_script_trait) {
					if (base.is_valid() && (base->has_script_trait(script_trait) || _script_conforms_to_trait(base, script_trait))) {
						return true;
					}
					// A native object (no Foundry Script instance), or a scripted object whose engine base
					// class was retroactively conformed, satisfies a trait-typed slot via the registry.
					return _native_class_conforms_to_trait(obj->get_class_name(), script_trait);
				}

				bool valid = false;
				while (base.is_valid()) {
					if (base == script_type) {
						valid = true;
						break;
					}
					base = base->get_base_script();
				}
				return valid;
			} break;
			case TYPE_PARAMETER: {
				// Type parameters are erased before execution; accept any value defensively.
				return true;
			} break;
		}
		return false;
	}

	bool can_contain_object() const {
		if (kind == BUILTIN) {
			switch (builtin_type) {
				case Variant::ARRAY:
					if (has_container_element_type(0)) {
						return container_element_types[0].can_contain_object();
					}
					return true;
				case Variant::DICTIONARY:
					if (has_container_element_types()) {
						return get_container_element_type_or_variant(0).can_contain_object() || get_container_element_type_or_variant(1).can_contain_object();
					}
					return true;
				case Variant::NIL:
				case Variant::OBJECT:
					return true;
				default:
					return false;
			}
		}
		return true;
	}

	void set_container_element_type(int p_index, const FSDataType &p_element_type) {
		ERR_FAIL_COND(p_index < 0);
		while (p_index >= container_element_types.size()) {
			container_element_types.push_back(FSDataType());
		}
		container_element_types.write[p_index] = FSDataType(p_element_type);
	}

	FSDataType get_container_element_type(int p_index) const {
		ERR_FAIL_INDEX_V(p_index, container_element_types.size(), FSDataType());
		return container_element_types[p_index];
	}

	FSDataType get_container_element_type_or_variant(int p_index) const {
		if (p_index < 0 || p_index >= container_element_types.size()) {
			return FSDataType();
		}
		return container_element_types[p_index];
	}

	bool has_container_element_type(int p_index) const {
		return p_index >= 0 && p_index < container_element_types.size();
	}

	bool has_container_element_types() const {
		return !container_element_types.is_empty();
	}

	ContainerType to_container_type() const {
		ContainerType type;
		if (is_nullable) {
			// Core typed containers cannot express "this type or null", so a nullable element type
			// becomes an untyped element. The analyzer still enforces element types statically.
			return type;
		}
		type.builtin_type = builtin_type;
		if (builtin_type == Variant::OBJECT) {
			type.class_name = native_type;
			if (script_type_ref.is_valid()) {
				type.script = script_type_ref;
			} else if (script_type != nullptr) {
				type.script.reference_ptr(script_type);
			}
		}
		for (const FSDataType &element_type : container_element_types) {
			type.element_types.push_back(element_type.to_container_type());
		}
		for (const FSDataType &argument_type : type_arguments) {
			type.type_arguments.push_back(argument_type.to_container_type());
		}
		return type;
	}

	bool is_same_container_type(const ContainerType &p_type) const {
		return to_container_type() == p_type;
	}

	FSDataType() = default;

	bool operator==(const FSDataType &p_other) const {
		return kind == p_other.kind &&
				builtin_type == p_other.builtin_type &&
				native_type == p_other.native_type &&
				is_nullable == p_other.is_nullable &&
				is_type_handle == p_other.is_type_handle &&
				is_self_type == p_other.is_self_type &&
				(script_type == p_other.script_type || script_type_ref == p_other.script_type_ref) &&
				is_script_trait == p_other.is_script_trait &&
				script_trait == p_other.script_trait &&
				container_element_types == p_other.container_element_types &&
				type_parameter_name == p_other.type_parameter_name &&
				type_parameter_scope == p_other.type_parameter_scope &&
				type_parameter_index == p_other.type_parameter_index &&
				type_arguments == p_other.type_arguments;
	}

	bool operator!=(const FSDataType &p_other) const {
		return !(*this == p_other);
	}

	void operator=(const FSDataType &p_other) {
		kind = p_other.kind;
		builtin_type = p_other.builtin_type;
		native_type = p_other.native_type;
		script_type = p_other.script_type;
		script_type_ref = p_other.script_type_ref;
		is_nullable = p_other.is_nullable;
		is_type_handle = p_other.is_type_handle;
		is_self_type = p_other.is_self_type;
		is_script_trait = p_other.is_script_trait;
		script_trait = p_other.script_trait;
		container_element_types = p_other.container_element_types;
		type_parameter_name = p_other.type_parameter_name;
		type_parameter_index = p_other.type_parameter_index;
		type_parameter_scope = p_other.type_parameter_scope;
		type_arguments = p_other.type_arguments;
	}

	FSDataType(const FSDataType &p_other) {
		*this = p_other;
	}

	~FSDataType() {}
};

class FSFunction {
public:
	// Set on the builtin-type operand of OPCODE_ASSIGN_TYPED_BUILTIN / OPCODE_RETURN_TYPED_BUILTIN to mark
	// the target as nullable, so a null source is stored as-is instead of being rejected or converted.
	// The flag sits well above Variant::VARIANT_MAX, so the real type is recovered by masking it off.
	static constexpr int NULLABLE_TYPE_OPERAND_FLAG = 1 << 24;

	// Each opcode's operand layout lives in THREE places that must stay in sync: the VM's dispatch in
	// `fs_vm.cpp` (authoritative), the `disassemble()` walk in `fs_disassembler.cpp`, and the link-time
	// bounds checker in `FSBytecodeVerifier::verify_function`. Adding, removing, or changing the
	// operands of any opcode requires updating all three and bumping `FSBytecodeFormat::FORMAT_VERSION`
	// (the verifier has no default fall-through: an opcode with no matching case is rejected as corrupt).
	enum Opcode {
		OPCODE_OPERATOR,
		OPCODE_OPERATOR_VALIDATED,
		OPCODE_TYPE_TEST_BUILTIN,
		OPCODE_TYPE_TEST_ARRAY,
		OPCODE_TYPE_TEST_DICTIONARY,
		OPCODE_TYPE_TEST_NATIVE,
		OPCODE_TYPE_TEST_SCRIPT,
		OPCODE_SET_KEYED,
		OPCODE_SET_KEYED_VALIDATED,
		OPCODE_SET_INDEXED_VALIDATED,
		OPCODE_GET_KEYED,
		OPCODE_GET_KEYED_VALIDATED,
		OPCODE_GET_INDEXED_VALIDATED,
		OPCODE_SET_NAMED,
		OPCODE_SET_NAMED_VALIDATED,
		OPCODE_GET_NAMED,
		OPCODE_GET_NAMED_VALIDATED,
		OPCODE_SET_MEMBER,
		OPCODE_GET_MEMBER,
		OPCODE_GET_TYPE_PARAMETER, // Reified script bound to the enclosing class's i-th type parameter.
		OPCODE_SET_STATIC_VARIABLE, // Only for FoundryScript.
		OPCODE_GET_STATIC_VARIABLE, // Only for FoundryScript.
		OPCODE_ASSIGN,
		OPCODE_ASSIGN_NULL,
		OPCODE_ASSIGN_TRUE,
		OPCODE_ASSIGN_FALSE,
		OPCODE_ASSIGN_TYPED_BUILTIN,
		OPCODE_ASSIGN_TYPED_ARRAY,
		OPCODE_ASSIGN_TYPED_DICTIONARY,
		OPCODE_ASSIGN_TYPED_NATIVE,
		OPCODE_ASSIGN_TYPED_SCRIPT,
		OPCODE_ASSIGN_TYPED_PARAMETER,
		OPCODE_ASSIGN_TYPED_ARRAY_CONVERT,
		OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT,
		OPCODE_CAST_TO_BUILTIN,
		OPCODE_CAST_TO_NATIVE,
		OPCODE_CAST_TO_SCRIPT,
		OPCODE_CONSTRUCT, // Only for basic types!
		OPCODE_CONSTRUCT_VALIDATED, // Only for basic types!
		OPCODE_CONSTRUCT_ARRAY,
		OPCODE_CONSTRUCT_TYPED_ARRAY,
		OPCODE_CONSTRUCT_TUPLE, // Builds the read-only Array a tuple value erases to.
		OPCODE_CONSTRUCT_DICTIONARY,
		OPCODE_CONSTRUCT_TYPED_DICTIONARY,
		OPCODE_CONSTRUCT_SPECIALIZED, // Instantiate a generic script with reified type arguments.
		OPCODE_CALL,
		OPCODE_CALL_RETURN,
		OPCODE_CALL_ASYNC,
		OPCODE_CALL_ENUM,
		OPCODE_CALL_ENUM_RETURN,
		OPCODE_CALL_ENUM_ASYNC,
		OPCODE_CALL_UTILITY,
		OPCODE_CALL_UTILITY_VALIDATED,
		OPCODE_CALL_FOUNDRY_SCRIPT_UTILITY,
		OPCODE_CALL_BUILTIN_TYPE_VALIDATED,
		OPCODE_CALL_SELF_BASE,
		OPCODE_CALL_METHOD_BIND,
		OPCODE_CALL_METHOD_BIND_RET,
		OPCODE_CALL_BUILTIN_STATIC,
		OPCODE_CALL_NATIVE_STATIC,
		OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN,
		OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN,
		OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN,
		OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN,
		OPCODE_AWAIT,
		OPCODE_AWAIT_RESUME,
		OPCODE_CREATE_LAMBDA,
		OPCODE_CREATE_SELF_LAMBDA,
		OPCODE_JUMP,
		OPCODE_JUMP_IF,
		OPCODE_JUMP_IF_NOT,
		OPCODE_JUMP_TO_DEF_ARGUMENT,
		OPCODE_JUMP_IF_SHARED,
		OPCODE_RETURN,
		OPCODE_RETURN_TYPED_BUILTIN,
		OPCODE_RETURN_TYPED_ARRAY,
		OPCODE_RETURN_TYPED_DICTIONARY,
		OPCODE_RETURN_TYPED_NATIVE,
		OPCODE_RETURN_TYPED_SCRIPT,
		OPCODE_ITERATE_BEGIN,
		OPCODE_ITERATE_BEGIN_INT,
		OPCODE_ITERATE_BEGIN_FLOAT,
		OPCODE_ITERATE_BEGIN_VECTOR2,
		OPCODE_ITERATE_BEGIN_VECTOR2I,
		OPCODE_ITERATE_BEGIN_VECTOR3,
		OPCODE_ITERATE_BEGIN_VECTOR3I,
		OPCODE_ITERATE_BEGIN_STRING,
		OPCODE_ITERATE_BEGIN_DICTIONARY,
		OPCODE_ITERATE_BEGIN_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_BYTE_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_INT32_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_INT64_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_FLOAT32_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_FLOAT64_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_STRING_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_VECTOR2_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_VECTOR3_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_COLOR_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_VECTOR4_ARRAY,
		OPCODE_ITERATE_BEGIN_OBJECT,
		OPCODE_ITERATE_BEGIN_RANGE,
		OPCODE_ITERATE,
		OPCODE_ITERATE_INT,
		OPCODE_ITERATE_FLOAT,
		OPCODE_ITERATE_VECTOR2,
		OPCODE_ITERATE_VECTOR2I,
		OPCODE_ITERATE_VECTOR3,
		OPCODE_ITERATE_VECTOR3I,
		OPCODE_ITERATE_STRING,
		OPCODE_ITERATE_DICTIONARY,
		OPCODE_ITERATE_ARRAY,
		OPCODE_ITERATE_PACKED_BYTE_ARRAY,
		OPCODE_ITERATE_PACKED_INT32_ARRAY,
		OPCODE_ITERATE_PACKED_INT64_ARRAY,
		OPCODE_ITERATE_PACKED_FLOAT32_ARRAY,
		OPCODE_ITERATE_PACKED_FLOAT64_ARRAY,
		OPCODE_ITERATE_PACKED_STRING_ARRAY,
		OPCODE_ITERATE_PACKED_VECTOR2_ARRAY,
		OPCODE_ITERATE_PACKED_VECTOR3_ARRAY,
		OPCODE_ITERATE_PACKED_COLOR_ARRAY,
		OPCODE_ITERATE_PACKED_VECTOR4_ARRAY,
		OPCODE_ITERATE_OBJECT,
		OPCODE_ITERATE_RANGE,
		OPCODE_STORE_GLOBAL,
		OPCODE_STORE_NAMED_GLOBAL,
		OPCODE_TYPE_ADJUST_BOOL,
		OPCODE_TYPE_ADJUST_INT,
		OPCODE_TYPE_ADJUST_FLOAT,
		OPCODE_TYPE_ADJUST_STRING,
		OPCODE_TYPE_ADJUST_VECTOR2,
		OPCODE_TYPE_ADJUST_VECTOR2I,
		OPCODE_TYPE_ADJUST_RECT2,
		OPCODE_TYPE_ADJUST_RECT2I,
		OPCODE_TYPE_ADJUST_VECTOR3,
		OPCODE_TYPE_ADJUST_VECTOR3I,
		OPCODE_TYPE_ADJUST_TRANSFORM2D,
		OPCODE_TYPE_ADJUST_VECTOR4,
		OPCODE_TYPE_ADJUST_VECTOR4I,
		OPCODE_TYPE_ADJUST_PLANE,
		OPCODE_TYPE_ADJUST_QUATERNION,
		OPCODE_TYPE_ADJUST_AABB,
		OPCODE_TYPE_ADJUST_BASIS,
		OPCODE_TYPE_ADJUST_TRANSFORM3D,
		OPCODE_TYPE_ADJUST_PROJECTION,
		OPCODE_TYPE_ADJUST_COLOR,
		OPCODE_TYPE_ADJUST_STRING_NAME,
		OPCODE_TYPE_ADJUST_NODE_PATH,
		OPCODE_TYPE_ADJUST_RID,
		OPCODE_TYPE_ADJUST_OBJECT,
		OPCODE_TYPE_ADJUST_CALLABLE,
		OPCODE_TYPE_ADJUST_SIGNAL,
		OPCODE_TYPE_ADJUST_DICTIONARY,
		OPCODE_TYPE_ADJUST_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY,
		OPCODE_ASSERT,
		OPCODE_BREAKPOINT,
		OPCODE_LINE,
		OPCODE_END
	};

	enum Address {
		ADDR_BITS = 24,
		ADDR_MASK = ((1 << ADDR_BITS) - 1),
		ADDR_TYPE_MASK = ~ADDR_MASK,
		ADDR_TYPE_STACK = 0,
		ADDR_TYPE_CONSTANT = 1,
		ADDR_TYPE_MEMBER = 2,
		ADDR_TYPE_MAX = 3,
	};

	enum FixedAddresses {
		ADDR_STACK_SELF = 0,
		ADDR_STACK_CLASS = 1,
		ADDR_STACK_NIL = 2,
		FIXED_ADDRESSES_MAX = 3,
		ADDR_SELF = ADDR_STACK_SELF | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_CLASS = ADDR_STACK_CLASS | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_NIL = ADDR_STACK_NIL | (ADDR_TYPE_STACK << ADDR_BITS),
	};

	enum ReflectionKind : uint8_t {
		REFLECTION_NONE = 0,
		REFLECTION_METHODS = 1 << 0,
		REFLECTION_PROPERTIES = 1 << 1,
		REFLECTION_SIGNALS = 1 << 2,
	};

	static uint8_t get_reflection_kind(const StringName &p_method, const StringName &p_class = StringName()) {
		if (p_method == SNAME("get_method_list") ||
				(p_class == SNAME("FSReflection") &&
						(p_method == SNAME("get_methods") ||
								p_method == SNAME("get_method_descriptors")))) {
			return REFLECTION_METHODS;
		}
		if (p_method == SNAME("get_property_list") ||
				(p_class == SNAME("FSReflection") &&
						(p_method == SNAME("get_properties") ||
								p_method == SNAME("get_property_descriptors")))) {
			return REFLECTION_PROPERTIES;
		}
		if (p_method == SNAME("get_signal_list")) {
			return REFLECTION_SIGNALS;
		}
		return REFLECTION_NONE;
	}

	struct StackDebug {
		int line;
		int pos;
		bool added;
		StringName identifier;
	};

private:
	friend class FoundryScript;
	friend class FSCompiler;
	friend class FSByteCodeGenerator;
	friend class FSLanguage;
	friend class FSBytecodeExporter;
	friend class FSBytecodeLoader;
	friend class FSBytecodeVerifier;
#ifdef TOOLS_ENABLED
	friend class FSNameManglerApplication;
	friend class FSNameManglerAnalysis;
#endif
#ifdef TESTS_ENABLED
	friend class FSTests::TestFSBytecodeScriptAccessor;
#endif // TESTS_ENABLED

	StringName name;
	StringName source;
	bool _static = false;
	Vector<FSDataType> argument_types;
	FSDataType return_type;
	MethodInfo method_info;
	Variant rpc_config;

	FoundryScript *_script = nullptr;
	int _initial_line = 0;
	int _argument_count = 0;
	int _vararg_index = -1;
	int _stack_size = 0;
	int _instruction_args_size = 0;

	SelfList<FSFunction> function_list{ this };
	mutable Variant nil;
	HashMap<int, Variant::Type> temporary_slots;
	List<StackDebug> stack_debug;

	Vector<int> code;
	Vector<int> default_arguments;
	Vector<Variant> constants;
	Vector<StringName> global_names;
	Vector<Variant::ValidatedOperatorEvaluator> operator_funcs;
	Vector<Variant::ValidatedSetter> setters;
	Vector<Variant::ValidatedGetter> getters;
	Vector<Variant::ValidatedKeyedSetter> keyed_setters;
	Vector<Variant::ValidatedKeyedGetter> keyed_getters;
	Vector<Variant::ValidatedIndexedSetter> indexed_setters;
	Vector<Variant::ValidatedIndexedGetter> indexed_getters;
	Vector<Variant::ValidatedBuiltInMethod> builtin_methods;
	Vector<Variant::ValidatedConstructor> constructors;
	Vector<Variant::ValidatedUtilityFunction> utilities;
	Vector<FSUtilityFunctions::FunctionPtr> gds_utilities;
	Vector<MethodBind *> methods;
	Vector<FSFunction *> lambdas;
	Vector<StringName> builtin_method_names;

	int _code_size = 0;
	int _default_arg_count = 0;
	int _constant_count = 0;
	int _global_names_count = 0;
	int _operator_funcs_count = 0;
	int _setters_count = 0;
	int _getters_count = 0;
	int _keyed_setters_count = 0;
	int _keyed_getters_count = 0;
	int _indexed_setters_count = 0;
	int _indexed_getters_count = 0;
	int _builtin_methods_count = 0;
	int _constructors_count = 0;
	int _utilities_count = 0;
	int _gds_utilities_count = 0;
	int _methods_count = 0;
	int _lambdas_count = 0;

	int *_code_ptr = nullptr;
	const int *_default_arg_ptr = nullptr;
	mutable Variant *_constants_ptr = nullptr;
	const StringName *_global_names_ptr = nullptr;
	const Variant::ValidatedOperatorEvaluator *_operator_funcs_ptr = nullptr;
	const Variant::ValidatedSetter *_setters_ptr = nullptr;
	const Variant::ValidatedGetter *_getters_ptr = nullptr;
	const Variant::ValidatedKeyedSetter *_keyed_setters_ptr = nullptr;
	const Variant::ValidatedKeyedGetter *_keyed_getters_ptr = nullptr;
	const Variant::ValidatedIndexedSetter *_indexed_setters_ptr = nullptr;
	const Variant::ValidatedIndexedGetter *_indexed_getters_ptr = nullptr;
	const Variant::ValidatedBuiltInMethod *_builtin_methods_ptr = nullptr;
	const Variant::ValidatedConstructor *_constructors_ptr = nullptr;
	const Variant::ValidatedUtilityFunction *_utilities_ptr = nullptr;
	const FSUtilityFunctions::FunctionPtr *_gds_utilities_ptr = nullptr;
	MethodBind **_methods_ptr = nullptr;
	FSFunction **_lambdas_ptr = nullptr;

#ifdef DEBUG_ENABLED
	CharString func_cname;
	const char *_func_cname = nullptr;

	Vector<String> operator_names;
	Vector<String> setter_names;
	Vector<String> getter_names;
	Vector<String> builtin_methods_names;
	Vector<String> constructors_names;
	Vector<String> utilities_names;
	Vector<String> gds_utilities_names;

	struct Profile {
		StringName signature;
		SafeNumeric<uint64_t> call_count;
		SafeNumeric<uint64_t> self_time;
		SafeNumeric<uint64_t> total_time;
		SafeNumeric<uint64_t> frame_call_count;
		SafeNumeric<uint64_t> frame_self_time;
		SafeNumeric<uint64_t> frame_total_time;
		uint64_t last_frame_call_count = 0;
		uint64_t last_frame_self_time = 0;
		uint64_t last_frame_total_time = 0;
		typedef struct NativeProfile {
			uint64_t call_count;
			uint64_t total_time;
			String signature;
		} NativeProfile;
		HashMap<String, NativeProfile> native_calls;
		HashMap<String, NativeProfile> last_native_calls;
	} profile;
#endif

#ifdef TOOLS_ENABLED

public:
	// Symbolic identities for every process-bound pointer the compiled function stores, recorded
	// by FSByteCodeGenerator so the bytecode exporter can re-resolve the pointers in another
	// process. Index i of each descriptor vector describes entry i of the matching pointer table.
	struct ExportFixups {
		struct OperatorKey {
			Variant::Operator op;
			Variant::Type left_type;
			Variant::Type right_type;
		};
		struct TypedNameKey {
			Variant::Type type;
			StringName name;
		};
		struct ConstructorKey {
			Variant::Type type;
			int constructor_index;
		};
		struct MethodBindKey {
			StringName class_name;
			StringName method_name;
		};
		struct GlobalStore {
			int code_offset; // Index into `code` of the baked global-array operand.
			StringName global_name;
		};
		Vector<OperatorKey> operators;
		Vector<TypedNameKey> setters;
		Vector<TypedNameKey> getters;
		Vector<Variant::Type> keyed_setters;
		Vector<Variant::Type> keyed_getters;
		Vector<Variant::Type> indexed_setters;
		Vector<Variant::Type> indexed_getters;
		Vector<TypedNameKey> builtin_methods;
		Vector<ConstructorKey> constructors;
		Vector<StringName> utilities;
		Vector<StringName> gds_utilities;
		Vector<MethodBindKey> method_binds;
		Vector<GlobalStore> global_stores;
		// Code offsets of every non-validated OPCODE_OPERATOR instruction. The VM patches inline-cache
		// words into that instruction at runtime (an operand signature, a cached return type, and a raw
		// evaluator function pointer); the exporter zeroes those words so no process-local pointer or
		// state is baked into a `.fsb`.
		Vector<int> operator_cache_offsets;
		Vector<StringName> named_globals; // For export-time validation only.
	};
	ExportFixups export_fixups;

private:
	uint8_t self_reflection_kinds = REFLECTION_NONE;
	uint8_t unresolved_reflection_kinds = REFLECTION_NONE;
#endif // TOOLS_ENABLED

	String _get_call_error(const String &p_where, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err) const;
	String _get_callable_call_error(const String &p_where, const Callable &p_callable, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err) const;
	Variant _get_default_variant_for_data_type(const FSDataType &p_data_type);

public:
	static constexpr int MAX_CALL_DEPTH = 2048; // Limit to try to avoid crash because of a stack overflow.

	struct CallState {
		Signal completed;
		FoundryScript *script = nullptr;
		FSInstance *instance = nullptr;
#ifdef DEBUG_ENABLED
		StringName function_name;
		String script_path;
#endif
		Vector<uint8_t> stack;
		int stack_size = 0;
		// A witness-style call has no FSInstance from which `self` can be reconstructed after an
		// await. Keep its explicit receiver alongside the copied non-reserved stack until resume.
		Variant self_override;
		bool has_self_override = false;
		int ip = 0;
		int line = 0;
		int defarg = 0;
		Variant result;
	};

	_FORCE_INLINE_ StringName get_name() const { return name; }
	_FORCE_INLINE_ StringName get_source() const { return source; }
	_FORCE_INLINE_ FoundryScript *get_script() const { return _script; }
	_FORCE_INLINE_ bool is_static() const { return _static; }
	_FORCE_INLINE_ bool is_vararg() const { return _vararg_index >= 0; }
	_FORCE_INLINE_ MethodInfo get_method_info() const { return method_info; }
	_FORCE_INLINE_ const FSDataType &get_return_type() const { return return_type; }
	_FORCE_INLINE_ int get_argument_count() const { return _argument_count; }
	_FORCE_INLINE_ Variant get_rpc_config() const { return rpc_config; }
	_FORCE_INLINE_ int get_max_stack_size() const { return _stack_size; }

	Variant get_constant(int p_idx) const;
	StringName get_global_name(int p_idx) const;

	// Re-establishes every `_*_count` / `_*_ptr` mirror field from the backing vectors. The byte
	// code generator and the compiled-bytecode loader both populate the vectors and then call this,
	// so the pointer-table invariants cannot drift between the two producers.
	void setup_runtime_pointers();

#ifdef TOOLS_ENABLED
	_FORCE_INLINE_ const Vector<int> &get_code() const { return code; }
	_FORCE_INLINE_ int get_operator_funcs_count() const { return _operator_funcs_count; }
	_FORCE_INLINE_ int get_setters_count() const { return _setters_count; }
	_FORCE_INLINE_ int get_getters_count() const { return _getters_count; }
	_FORCE_INLINE_ int get_keyed_setters_count() const { return _keyed_setters_count; }
	_FORCE_INLINE_ int get_keyed_getters_count() const { return _keyed_getters_count; }
	_FORCE_INLINE_ int get_indexed_setters_count() const { return _indexed_setters_count; }
	_FORCE_INLINE_ int get_indexed_getters_count() const { return _indexed_getters_count; }
	_FORCE_INLINE_ int get_builtin_methods_count() const { return _builtin_methods_count; }
	_FORCE_INLINE_ int get_constructors_count() const { return _constructors_count; }
	_FORCE_INLINE_ int get_utilities_count() const { return _utilities_count; }
	_FORCE_INLINE_ int get_gds_utilities_count() const { return _gds_utilities_count; }
	_FORCE_INLINE_ int get_methods_count() const { return _methods_count; }
	_FORCE_INLINE_ int get_constants_count() const { return _constant_count; }
	_FORCE_INLINE_ int get_global_names_count() const { return _global_names_count; }
	_FORCE_INLINE_ int get_lambdas_count() const { return _lambdas_count; }
	_FORCE_INLINE_ const Vector<FSFunction *> &get_lambdas() const { return lambdas; }
	_FORCE_INLINE_ const Vector<int> &get_default_argument_offsets() const { return default_arguments; }
	_FORCE_INLINE_ int get_instruction_args_size() const { return _instruction_args_size; }
#endif // TOOLS_ENABLED

	Variant call(FSInstance *p_instance, const Variant **p_args, int p_argcount, Callable::CallError &r_err, CallState *p_state = nullptr, const Variant *p_self_override = nullptr);
	// Dispatches a retroactive-conformance witness on a receiver that has no FSInstance (a native engine
	// object or, later, a builtin value). The witness was compiled against the target's surface, so
	// `self` is bound to `p_self` and member access rides the native/builtin access opcodes; no FS member
	// layout is available, so the witness must not reference instance members.
	Variant call_witness(const Variant &p_self, const Variant **p_args, int p_argcount, Callable::CallError &r_err);
	void debug_get_stack_member_state(int p_line, List<Pair<StringName, int>> *r_stackvars) const;

#ifdef DEBUG_ENABLED
	void _profile_native_call(uint64_t p_t_taken, const String &p_function_name, const String &p_instance_class_name = String());
	void disassemble(const Vector<String> &p_code_lines) const;
#endif

	FSFunction();
	~FSFunction();
};

class FSFunctionState : public ScriptFunctionState {
	FOUNDRY_CLASS(FSFunctionState, ScriptFunctionState);
	friend class FSFunction;
	FSFunction *function = nullptr;
	FSFunction::CallState state;
	Variant _signal_callback(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Ref<FSFunctionState> first_state;

	SelfList<FSFunctionState> scripts_list;
	SelfList<FSFunctionState> instances_list;

protected:
	static void _bind_methods();

public:
	bool is_valid(bool p_extended_check = false) const;
	Variant resume(const Variant &p_arg = Variant());

#ifdef DEBUG_ENABLED
	// Returns a human-readable representation of the function.
	String get_readable_function() {
		return state.function_name;
	}
#endif

	void _clear_stack();
	void _clear_connections();
	static void abandon_chain(FSFunctionState *p_state);

	FSFunctionState();
	~FSFunctionState();
};
