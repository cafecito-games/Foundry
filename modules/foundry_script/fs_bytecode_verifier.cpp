/**************************************************************************/
/*  fs_bytecode_verifier.cpp                                              */
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

#include "fs_bytecode_verifier.h"

#include "foundry_script.h"
#include "fs_function.h"

#include "core/templates/local_vector.h"

// Reject a corrupt opcode stream with an accurate, deterministic message. The message never depends
// on run order or the environment, so a failed load is diagnosable from logs alone.
#define VERIFY_FAIL_COND(m_condition, m_reason)                                                                   \
	ERR_FAIL_COND_V_MSG(m_condition, ERR_INVALID_DATA,                                                            \
			vformat("Malformed compiled function '%s' in script '%s': %s.", String(function_name), p_script_path, \
					String(m_reason)))

Error FSBytecodeVerifier::verify_function(const FSFunction *p_function, int p_member_address_count, const String &p_script_path) {
	ERR_FAIL_NULL_V(p_function, ERR_INVALID_PARAMETER);

	const String function_name = p_function->name;
	const Vector<int> &code = p_function->code;
	const int code_size = code.size();
	const int *code_ptr = code.ptr();

	// Address-space upper bounds, indexed by the address tag packed into the top bits of an operand.
	// These mirror `variant_address_limits` in the VM (`fs_vm.cpp`): stack slots, function constants,
	// and the owning class's members.
	const int address_limits[FSFunction::ADDR_TYPE_MAX] = {
		p_function->_stack_size,
		(int)p_function->constants.size(),
		p_member_address_count,
	};

	const int operator_funcs_count = p_function->operator_funcs.size();
	const int setters_count = p_function->setters.size();
	const int getters_count = p_function->getters.size();
	const int keyed_setters_count = p_function->keyed_setters.size();
	const int keyed_getters_count = p_function->keyed_getters.size();
	const int indexed_setters_count = p_function->indexed_setters.size();
	const int indexed_getters_count = p_function->indexed_getters.size();
	const int builtin_methods_count = p_function->builtin_methods.size();
	const int constructors_count = p_function->constructors.size();
	const int utilities_count = p_function->utilities.size();
	const int gds_utilities_count = p_function->gds_utilities.size();
	const int methods_count = p_function->methods.size();
	const int lambdas_count = p_function->lambdas.size();
	const int global_names_count = p_function->global_names.size();

	const FSLanguage *language = FSLanguage::get_singleton();
	const int global_array_size = language != nullptr ? language->get_global_array_size() : 0;

	// Marks each offset that begins an instruction; `code_size` is the valid one-past-end target of a
	// loop-exit jump. A jump into the middle of an instruction never lands on a marked boundary.
	LocalVector<bool> instruction_starts;
	instruction_starts.resize(code_size + 1);
	for (int i = 0; i <= code_size; i++) {
		instruction_starts[i] = false;
	}
	instruction_starts[code_size] = true;

	// Every jump/branch/iterate target and default-argument entry, validated after the walk once all
	// instruction boundaries are known (targets may point forward).
	LocalVector<int> jump_targets;

	// Validates a packed address operand at an already-in-bounds code offset.
	const auto check_address = [&](int p_operand_offset) -> bool {
		const int address = code_ptr[p_operand_offset];
		const int address_type = (address & FSFunction::ADDR_TYPE_MASK) >> FSFunction::ADDR_BITS;
		if (address_type < 0 || address_type >= FSFunction::ADDR_TYPE_MAX) {
			return false;
		}
		const int address_index = address & FSFunction::ADDR_MASK;
		return address_index >= 0 && address_index < address_limits[address_type];
	};

#define CHECK_ADDR(m_offset) VERIFY_FAIL_COND(!check_address((m_offset)), "operand address is out of range")
#define CHECK_TABLE(m_offset, m_size, m_label)                                                  \
	do {                                                                                        \
		const int m_index = code_ptr[(m_offset)];                                               \
		VERIFY_FAIL_COND(m_index < 0 || m_index >= (m_size), m_label " index is out of range"); \
	} while (false)
#define COLLECT_JUMP(m_offset) jump_targets.push_back(code_ptr[(m_offset)])

	constexpr int operator_pointer_size = sizeof(Variant::ValidatedOperatorEvaluator) / sizeof(int);

	int ip = 0;
	while (ip < code_size) {
		instruction_starts[ip] = true;
		const int opcode = code_ptr[ip];

		switch (opcode) {
			case FSFunction::OPCODE_OPERATOR: {
				const int length = 7 + operator_pointer_size;
				VERIFY_FAIL_COND(ip + length > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				ip += length;
			} break;
			case FSFunction::OPCODE_OPERATOR_VALIDATED: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_TABLE(ip + 4, operator_funcs_count, "operator");
				ip += 5;
			} break;
			case FSFunction::OPCODE_TYPE_TEST_BUILTIN: {
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				ip += 4;
			} break;
			case FSFunction::OPCODE_TYPE_TEST_ARRAY:
			case FSFunction::OPCODE_ASSIGN_TYPED_ARRAY:
			case FSFunction::OPCODE_ASSIGN_TYPED_ARRAY_CONVERT: {
				VERIFY_FAIL_COND(ip + 6 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_TABLE(ip + 5, global_names_count, "global name");
				ip += 6;
			} break;
			case FSFunction::OPCODE_TYPE_TEST_DICTIONARY:
			case FSFunction::OPCODE_ASSIGN_TYPED_DICTIONARY:
			case FSFunction::OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT: {
				VERIFY_FAIL_COND(ip + 9 > code_size, "instruction overruns code");
				// A typed dictionary carries two type-info addresses: the key at ip+3 and the value at
				// ip+4 (the VM reads both through GET_VARIANT_PTR and dereferences them).
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_ADDR(ip + 4);
				CHECK_TABLE(ip + 6, global_names_count, "global name");
				CHECK_TABLE(ip + 8, global_names_count, "global name");
				ip += 9;
			} break;
			case FSFunction::OPCODE_TYPE_TEST_NATIVE: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_TABLE(ip + 3, global_names_count, "global name");
				ip += 5;
			} break;
			case FSFunction::OPCODE_TYPE_TEST_SCRIPT:
			case FSFunction::OPCODE_ASSIGN_TYPED_NATIVE:
			case FSFunction::OPCODE_ASSIGN_TYPED_SCRIPT:
			case FSFunction::OPCODE_CAST_TO_NATIVE:
			case FSFunction::OPCODE_CAST_TO_SCRIPT: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				ip += 5;
			} break;
			case FSFunction::OPCODE_SET_KEYED:
			case FSFunction::OPCODE_GET_KEYED: {
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				ip += 4;
			} break;
			case FSFunction::OPCODE_SET_KEYED_VALIDATED: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_TABLE(ip + 4, keyed_setters_count, "keyed setter");
				ip += 5;
			} break;
			case FSFunction::OPCODE_SET_INDEXED_VALIDATED: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_TABLE(ip + 4, indexed_setters_count, "indexed setter");
				ip += 5;
			} break;
			case FSFunction::OPCODE_GET_KEYED_VALIDATED: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_TABLE(ip + 4, keyed_getters_count, "keyed getter");
				ip += 5;
			} break;
			case FSFunction::OPCODE_GET_INDEXED_VALIDATED: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_TABLE(ip + 4, indexed_getters_count, "indexed getter");
				ip += 5;
			} break;
			case FSFunction::OPCODE_SET_NAMED:
			case FSFunction::OPCODE_GET_NAMED: {
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_TABLE(ip + 3, global_names_count, "global name");
				ip += 4;
			} break;
			case FSFunction::OPCODE_SET_NAMED_VALIDATED: {
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_TABLE(ip + 3, setters_count, "setter");
				ip += 4;
			} break;
			case FSFunction::OPCODE_GET_NAMED_VALIDATED: {
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_TABLE(ip + 3, getters_count, "getter");
				ip += 4;
			} break;
			case FSFunction::OPCODE_SET_MEMBER:
			case FSFunction::OPCODE_GET_MEMBER: {
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_TABLE(ip + 2, global_names_count, "global name");
				ip += 3;
			} break;
			case FSFunction::OPCODE_GET_TYPE_PARAMETER: {
				// The type-parameter ordinal at ip+2 is bounds-checked at execution against the
				// resolved binding table, which is release-safe, so only the destination is checked.
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				ip += 3;
			} break;
			case FSFunction::OPCODE_SET_STATIC_VARIABLE:
			case FSFunction::OPCODE_GET_STATIC_VARIABLE: {
				// The static-variable slot at ip+3 indexes the target class's static table, whose size
				// is a runtime property of a class reached through the operand at ip+2; only a negative
				// slot is statically rejectable here (documented as a residual, instance-dependent bound).
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				VERIFY_FAIL_COND(code_ptr[ip + 3] < 0, "static variable slot is negative");
				ip += 4;
			} break;
			case FSFunction::OPCODE_ASSIGN: {
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				ip += 3;
			} break;
			case FSFunction::OPCODE_ASSIGN_NULL:
			case FSFunction::OPCODE_ASSIGN_TRUE:
			case FSFunction::OPCODE_ASSIGN_FALSE: {
				VERIFY_FAIL_COND(ip + 2 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				ip += 2;
			} break;
			case FSFunction::OPCODE_ASSIGN_TYPED_BUILTIN:
			case FSFunction::OPCODE_ASSIGN_TYPED_PARAMETER:
			case FSFunction::OPCODE_CAST_TO_BUILTIN: {
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				ip += 4;
			} break;
			case FSFunction::OPCODE_AWAIT:
			case FSFunction::OPCODE_AWAIT_RESUME:
			case FSFunction::OPCODE_RETURN: {
				VERIFY_FAIL_COND(ip + 2 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				ip += 2;
			} break;
			case FSFunction::OPCODE_RETURN_TYPED_BUILTIN: {
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				ip += 3;
			} break;
			case FSFunction::OPCODE_RETURN_TYPED_ARRAY: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_TABLE(ip + 4, global_names_count, "global name");
				ip += 5;
			} break;
			case FSFunction::OPCODE_RETURN_TYPED_DICTIONARY: {
				VERIFY_FAIL_COND(ip + 8 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_TABLE(ip + 5, global_names_count, "global name");
				CHECK_TABLE(ip + 7, global_names_count, "global name");
				ip += 8;
			} break;
			case FSFunction::OPCODE_RETURN_TYPED_NATIVE:
			case FSFunction::OPCODE_RETURN_TYPED_SCRIPT: {
				VERIFY_FAIL_COND(ip + 4 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				ip += 4;
			} break;
			case FSFunction::OPCODE_JUMP: {
				VERIFY_FAIL_COND(ip + 2 > code_size, "instruction overruns code");
				COLLECT_JUMP(ip + 1);
				ip += 2;
			} break;
			case FSFunction::OPCODE_JUMP_IF:
			case FSFunction::OPCODE_JUMP_IF_NOT:
			case FSFunction::OPCODE_JUMP_IF_SHARED: {
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				COLLECT_JUMP(ip + 2);
				ip += 3;
			} break;
			case FSFunction::OPCODE_JUMP_TO_DEF_ARGUMENT: {
				VERIFY_FAIL_COND(ip + 1 > code_size, "instruction overruns code");
				ip += 1;
			} break;
			case FSFunction::OPCODE_ITERATE_BEGIN:
			case FSFunction::OPCODE_ITERATE_BEGIN_INT:
			case FSFunction::OPCODE_ITERATE_BEGIN_FLOAT:
			case FSFunction::OPCODE_ITERATE_BEGIN_VECTOR2:
			case FSFunction::OPCODE_ITERATE_BEGIN_VECTOR2I:
			case FSFunction::OPCODE_ITERATE_BEGIN_VECTOR3:
			case FSFunction::OPCODE_ITERATE_BEGIN_VECTOR3I:
			case FSFunction::OPCODE_ITERATE_BEGIN_STRING:
			case FSFunction::OPCODE_ITERATE_BEGIN_DICTIONARY:
			case FSFunction::OPCODE_ITERATE_BEGIN_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_BYTE_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_INT32_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_INT64_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_FLOAT32_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_FLOAT64_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_STRING_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_VECTOR2_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_VECTOR3_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_COLOR_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_PACKED_VECTOR4_ARRAY:
			case FSFunction::OPCODE_ITERATE_BEGIN_OBJECT:
			case FSFunction::OPCODE_ITERATE:
			case FSFunction::OPCODE_ITERATE_INT:
			case FSFunction::OPCODE_ITERATE_FLOAT:
			case FSFunction::OPCODE_ITERATE_VECTOR2:
			case FSFunction::OPCODE_ITERATE_VECTOR2I:
			case FSFunction::OPCODE_ITERATE_VECTOR3:
			case FSFunction::OPCODE_ITERATE_VECTOR3I:
			case FSFunction::OPCODE_ITERATE_STRING:
			case FSFunction::OPCODE_ITERATE_DICTIONARY:
			case FSFunction::OPCODE_ITERATE_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_BYTE_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_INT32_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_INT64_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_FLOAT32_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_FLOAT64_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_STRING_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_VECTOR2_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_VECTOR3_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_COLOR_ARRAY:
			case FSFunction::OPCODE_ITERATE_PACKED_VECTOR4_ARRAY:
			case FSFunction::OPCODE_ITERATE_OBJECT: {
				VERIFY_FAIL_COND(ip + 5 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				COLLECT_JUMP(ip + 4);
				ip += 5;
			} break;
			case FSFunction::OPCODE_ITERATE_BEGIN_RANGE: {
				VERIFY_FAIL_COND(ip + 7 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_ADDR(ip + 4);
				CHECK_ADDR(ip + 5);
				COLLECT_JUMP(ip + 6);
				ip += 7;
			} break;
			case FSFunction::OPCODE_ITERATE_RANGE: {
				VERIFY_FAIL_COND(ip + 6 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				CHECK_ADDR(ip + 3);
				CHECK_ADDR(ip + 4);
				COLLECT_JUMP(ip + 5);
				ip += 6;
			} break;
			case FSFunction::OPCODE_STORE_GLOBAL: {
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_TABLE(ip + 2, global_array_size, "language global");
				ip += 3;
			} break;
			case FSFunction::OPCODE_STORE_NAMED_GLOBAL: {
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_TABLE(ip + 2, global_names_count, "global name");
				ip += 3;
			} break;
			case FSFunction::OPCODE_TYPE_ADJUST_BOOL:
			case FSFunction::OPCODE_TYPE_ADJUST_INT:
			case FSFunction::OPCODE_TYPE_ADJUST_FLOAT:
			case FSFunction::OPCODE_TYPE_ADJUST_STRING:
			case FSFunction::OPCODE_TYPE_ADJUST_VECTOR2:
			case FSFunction::OPCODE_TYPE_ADJUST_VECTOR2I:
			case FSFunction::OPCODE_TYPE_ADJUST_RECT2:
			case FSFunction::OPCODE_TYPE_ADJUST_RECT2I:
			case FSFunction::OPCODE_TYPE_ADJUST_VECTOR3:
			case FSFunction::OPCODE_TYPE_ADJUST_VECTOR3I:
			case FSFunction::OPCODE_TYPE_ADJUST_TRANSFORM2D:
			case FSFunction::OPCODE_TYPE_ADJUST_VECTOR4:
			case FSFunction::OPCODE_TYPE_ADJUST_VECTOR4I:
			case FSFunction::OPCODE_TYPE_ADJUST_PLANE:
			case FSFunction::OPCODE_TYPE_ADJUST_QUATERNION:
			case FSFunction::OPCODE_TYPE_ADJUST_AABB:
			case FSFunction::OPCODE_TYPE_ADJUST_BASIS:
			case FSFunction::OPCODE_TYPE_ADJUST_TRANSFORM3D:
			case FSFunction::OPCODE_TYPE_ADJUST_PROJECTION:
			case FSFunction::OPCODE_TYPE_ADJUST_COLOR:
			case FSFunction::OPCODE_TYPE_ADJUST_STRING_NAME:
			case FSFunction::OPCODE_TYPE_ADJUST_NODE_PATH:
			case FSFunction::OPCODE_TYPE_ADJUST_RID:
			case FSFunction::OPCODE_TYPE_ADJUST_OBJECT:
			case FSFunction::OPCODE_TYPE_ADJUST_CALLABLE:
			case FSFunction::OPCODE_TYPE_ADJUST_SIGNAL:
			case FSFunction::OPCODE_TYPE_ADJUST_DICTIONARY:
			case FSFunction::OPCODE_TYPE_ADJUST_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY:
			case FSFunction::OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY: {
				VERIFY_FAIL_COND(ip + 2 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				ip += 2;
			} break;
			case FSFunction::OPCODE_ASSERT: {
				VERIFY_FAIL_COND(ip + 3 > code_size, "instruction overruns code");
				CHECK_ADDR(ip + 1);
				CHECK_ADDR(ip + 2);
				ip += 3;
			} break;
			case FSFunction::OPCODE_LINE: {
				VERIFY_FAIL_COND(ip + 2 > code_size, "instruction overruns code");
				ip += 2;
			} break;
			case FSFunction::OPCODE_BREAKPOINT:
			case FSFunction::OPCODE_END: {
				VERIFY_FAIL_COND(ip + 1 > code_size, "instruction overruns code");
				ip += 1;
			} break;

			// Variable-argument opcodes. Their instruction-argument operands (`instruction_args`) are
			// the `instr_arg_count` packed addresses at ip+2..ip+1+instr_arg_count, all of which the VM
			// reads through the bounds-checked address path. The trailing fields (argument count, table
			// index, type descriptors) sit after them. Beyond the address and table checks, the highest
			// `instruction_args` index the opcode dereferences must stay inside the populated range, or
			// the VM reads an uninitialized pointer.
			case FSFunction::OPCODE_CONSTRUCT:
			case FSFunction::OPCODE_CONSTRUCT_VALIDATED:
			case FSFunction::OPCODE_CONSTRUCT_ARRAY:
			case FSFunction::OPCODE_CONSTRUCT_TYPED_ARRAY:
			case FSFunction::OPCODE_CONSTRUCT_DICTIONARY:
			case FSFunction::OPCODE_CONSTRUCT_TYPED_DICTIONARY:
			case FSFunction::OPCODE_CONSTRUCT_SPECIALIZED:
			case FSFunction::OPCODE_CALL:
			case FSFunction::OPCODE_CALL_RETURN:
			case FSFunction::OPCODE_CALL_ASYNC:
			case FSFunction::OPCODE_CALL_METHOD_BIND:
			case FSFunction::OPCODE_CALL_METHOD_BIND_RET:
			case FSFunction::OPCODE_CALL_BUILTIN_STATIC:
			case FSFunction::OPCODE_CALL_NATIVE_STATIC:
			case FSFunction::OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN:
			case FSFunction::OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN:
			case FSFunction::OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN:
			case FSFunction::OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN:
			case FSFunction::OPCODE_CALL_BUILTIN_TYPE_VALIDATED:
			case FSFunction::OPCODE_CALL_UTILITY:
			case FSFunction::OPCODE_CALL_UTILITY_VALIDATED:
			case FSFunction::OPCODE_CALL_FOUNDRY_SCRIPT_UTILITY:
			case FSFunction::OPCODE_CALL_SELF_BASE:
			case FSFunction::OPCODE_CREATE_LAMBDA:
			case FSFunction::OPCODE_CREATE_SELF_LAMBDA: {
				VERIFY_FAIL_COND(ip + 2 > code_size, "instruction argument count overruns code");
				const int instruction_arg_count = code_ptr[ip + 1];
				VERIFY_FAIL_COND(instruction_arg_count < 0, "negative instruction argument count");
				// The count is an attacker-controlled raw code word. Cap it against the code size before
				// any `ip + count`/`ip + length` arithmetic below: a value near INT_MAX would otherwise
				// overflow those signed sums past `code_size` and slip through the overrun guard (and the
				// argument loop). A function cannot have more argument words than it has code words, so
				// this is a true invariant and keeps every offset computed here well inside `int`.
				VERIFY_FAIL_COND(instruction_arg_count > code_size, "instruction argument count exceeds code size");

				// `shift` is where the VM's `ip` points after loading the arguments (`ip += 1` in
				// LOAD_INSTRUCTION_ARGS, then `ip += instr_arg_count`); trailing fields are read at
				// `shift + n` and the last address sits at `shift`.
				const int shift = ip + 1 + instruction_arg_count;

				// Trailing tail length past `shift`, matching each opcode's final `ip += K` in the VM.
				int tail = 3;
				switch (opcode) {
					case FSFunction::OPCODE_CONSTRUCT_ARRAY:
					case FSFunction::OPCODE_CONSTRUCT_DICTIONARY:
						tail = 2;
						break;
					case FSFunction::OPCODE_CONSTRUCT_TYPED_ARRAY:
					case FSFunction::OPCODE_CALL_BUILTIN_STATIC:
						tail = 4;
						break;
					case FSFunction::OPCODE_CONSTRUCT_TYPED_DICTIONARY:
						tail = 6;
						break;
					default:
						tail = 3;
						break;
				}
				const int length = 1 + instruction_arg_count + tail;
				VERIFY_FAIL_COND(ip + length > code_size, "instruction overruns code");

				for (int argument = 0; argument < instruction_arg_count; argument++) {
					CHECK_ADDR(ip + 2 + argument);
				}

				// Read the argument count and any table index at the exact per-opcode offsets the VM
				// uses (relative to `shift`), then bound the highest `instruction_args` index used.
				int64_t highest_arg_index = -1;
				switch (opcode) {
					case FSFunction::OPCODE_CONSTRUCT: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CONSTRUCT_VALIDATED: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, constructors_count, "constructor");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CONSTRUCT_ARRAY: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CONSTRUCT_TYPED_ARRAY: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 3, global_names_count, "global name");
						highest_arg_index = (int64_t)argument_count + 1;
					} break;
					case FSFunction::OPCODE_CONSTRUCT_DICTIONARY: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						highest_arg_index = (int64_t)argument_count * 2;
					} break;
					case FSFunction::OPCODE_CONSTRUCT_TYPED_DICTIONARY: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 3, global_names_count, "global name");
						CHECK_TABLE(shift + 5, global_names_count, "global name");
						highest_arg_index = (int64_t)argument_count * 2 + 2;
					} break;
					case FSFunction::OPCODE_CONSTRUCT_SPECIALIZED: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						const int type_argument_count = code_ptr[shift + 2];
						VERIFY_FAIL_COND(type_argument_count < 0, "negative type argument count");
						highest_arg_index = (int64_t)argument_count + type_argument_count + 2;
					} break;
					case FSFunction::OPCODE_CALL: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, global_names_count, "global name");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_RETURN:
					case FSFunction::OPCODE_CALL_ASYNC: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, global_names_count, "global name");
						highest_arg_index = (int64_t)argument_count + 1;
					} break;
					case FSFunction::OPCODE_CALL_METHOD_BIND: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, methods_count, "method bind");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_METHOD_BIND_RET:
					case FSFunction::OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN:
					case FSFunction::OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, methods_count, "method bind");
						highest_arg_index = (int64_t)argument_count + 1;
					} break;
					case FSFunction::OPCODE_CALL_BUILTIN_STATIC: {
						const int argument_count = code_ptr[shift + 3];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, global_names_count, "global name");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_NATIVE_STATIC: {
						CHECK_TABLE(shift + 1, methods_count, "method bind");
						const int argument_count = code_ptr[shift + 2];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN:
					case FSFunction::OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, methods_count, "method bind");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_BUILTIN_TYPE_VALIDATED: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, builtin_methods_count, "builtin method");
						highest_arg_index = (int64_t)argument_count + 1;
					} break;
					case FSFunction::OPCODE_CALL_UTILITY: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, global_names_count, "global name");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_UTILITY_VALIDATED: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, utilities_count, "utility");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_FOUNDRY_SCRIPT_UTILITY: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, gds_utilities_count, "script utility");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CALL_SELF_BASE: {
						const int argument_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(argument_count < 0, "negative argument count");
						CHECK_TABLE(shift + 2, global_names_count, "global name");
						highest_arg_index = argument_count;
					} break;
					case FSFunction::OPCODE_CREATE_LAMBDA:
					case FSFunction::OPCODE_CREATE_SELF_LAMBDA: {
						const int capture_count = code_ptr[shift + 1];
						VERIFY_FAIL_COND(capture_count < 0, "negative capture count");
						CHECK_TABLE(shift + 2, lambdas_count, "lambda");
						highest_arg_index = capture_count;
					} break;
					default:
						break;
				}
				VERIFY_FAIL_COND(highest_arg_index >= (int64_t)instruction_arg_count,
						"instruction argument index is out of range");

				ip += length;
			} break;

			default: {
				VERIFY_FAIL_COND(true, vformat("unknown opcode %d", opcode));
			} break;
		}
	}

	VERIFY_FAIL_COND(ip != code_size, "instruction stream does not end on an instruction boundary");

	// A jump/branch/iterate target must land on an instruction start or be the one-past-end loop exit.
	for (const int target : jump_targets) {
		VERIFY_FAIL_COND(target < 0 || target > code_size || !instruction_starts[target],
				"jump target does not land on an instruction boundary");
	}
	// Default-argument entries are jump targets the VM reaches through OPCODE_JUMP_TO_DEF_ARGUMENT.
	for (const int target : p_function->default_arguments) {
		VERIFY_FAIL_COND(target < 0 || target > code_size || !instruction_starts[target],
				"default argument target does not land on an instruction boundary");
	}

#undef CHECK_ADDR
#undef CHECK_TABLE
#undef COLLECT_JUMP
#undef VERIFY_FAIL_COND

	return OK;
}
