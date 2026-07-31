/**************************************************************************/
/*  test_bytecode_hardening.h                                             */
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

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_bytecode_verifier.h"
#include "modules/foundry_script/tests/test_bytecode_script.h"

namespace FSTests {

// Overwrites a deserialized function's opcode stream with a hand-built instruction and re-runs the
// verifier. The function was produced by a real load, so its address-space sizes and tables are
// consistent; only the crafted opcode stream is under test.
static Error bytecode_verify_with_code(const Ref<FoundryScript> &p_script, FSFunction *p_function, const Vector<int> &p_code) {
	Vector<int> &code = const_cast<Vector<int> &>(p_function->get_code());
	code = p_code;
	// A crafted stream is expected to be rejected, so silence the diagnostic the verifier prints.
	ERR_PRINT_OFF;
	const Error error = FSBytecodeVerifier::verify_function(p_function, p_script->debug_get_member_indices().size(), p_script->get_script_path());
	ERR_PRINT_ON;
	return error;
}

TEST_CASE("[FoundryScript][BytecodeHardening] Format version is pinned") {
	// The reader rejects any other version outright, so the on-disk layout and this constant move
	// together. Bump FORMAT_VERSION in the same change as ANY layout change to the `.fsb` format
	// (sections, field order/width, opcode operand layout, tag/fixup sets) and update this pin.
	CHECK(FSBytecodeFormat::FORMAT_VERSION == 6);
}

TEST_CASE("[FoundryScript][BytecodeHardening] Loader rejects a stale format version") {
	Vector<uint8_t> header = FSBytecodeExporter::write_header();
	// The format version is the first u32 after the 4-byte magic; corrupt it to a version that
	// predates the tuple opcode/data-type additions and confirm the loader refuses to read it
	// instead of silently decoding a layout it no longer matches.
	REQUIRE(header.size() >= 8);
	const uint32_t stale_version = FSBytecodeFormat::FORMAT_VERSION - 1;
	memcpy(header.ptrw() + 4, &stale_version, sizeof(uint32_t));
	ERR_PRINT_OFF;
	CHECK(FSBytecodeLoader::check_header(header) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier validates every enum-call operand and identity") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func placeholder() -> void:\n"
			"\tpass\n");

	struct EnumCallCase {
		FSFunction::Opcode opcode;
		bool has_target;
		bool is_async;
	};
	const EnumCallCase opcode_cases[] = {
		{ FSFunction::OPCODE_CALL_ENUM, false, false },
		{ FSFunction::OPCODE_CALL_ENUM_RETURN, true, false },
		{ FSFunction::OPCODE_CALL_ENUM_ASYNC, true, true },
	};

	for (const EnumCallCase &opcode_case : opcode_cases) {
		FSByteCodeGenerator generator;
		generator.write_start(script.ptr(), vformat("enum_call_%d", (int)opcode_case.opcode), true, Variant(), FSDataType());

		const uint32_t receiver_index = generator.add_or_get_constant(1);
		const FSCodeGenerator::Address base(FSCodeGenerator::Address::CONSTANT, receiver_index);
		FSCodeGenerator::Address target;
		if (opcode_case.has_target) {
			const uint32_t target_index = generator.add_temporary(FSDataType());
			target = FSCodeGenerator::Address(FSCodeGenerator::Address::TEMPORARY, target_index);
		}
		generator.write_enum_call(target, base, Vector<FSCodeGenerator::Address>(),
				SNAME("res://enum_owner.fs"), SNAME("Owner"), SNAME("Status"), SNAME("resolve"),
				false, opcode_case.is_async);
		if (opcode_case.has_target) {
			generator.pop_temporary();
		}
		FSFunction *function = generator.write_end();
		REQUIRE(function != nullptr);

		const Vector<int> valid = function->get_code();
		REQUIRE(valid.size() == 11);
		CHECK(valid[0] == opcode_case.opcode);
		CHECK(bytecode_verify_with_code(script, function, valid) == OK);

		Vector<int> truncated = valid;
		truncated.resize(9);
		CHECK(bytecode_verify_with_code(script, function, truncated) == ERR_INVALID_DATA);

		Vector<int> negative_argument_count = valid;
		negative_argument_count.write[4] = -1;
		CHECK(bytecode_verify_with_code(script, function, negative_argument_count) == ERR_INVALID_DATA);

		Vector<int> mismatched_argument_count = valid;
		mismatched_argument_count.write[4] = 1;
		CHECK(bytecode_verify_with_code(script, function, mismatched_argument_count) == ERR_INVALID_DATA);

		const int out_of_range_stack =
				FSFunction::ADDR_TYPE_STACK << FSFunction::ADDR_BITS | function->get_max_stack_size();
		Vector<int> bad_receiver = valid;
		bad_receiver.write[2] = out_of_range_stack;
		CHECK(bytecode_verify_with_code(script, function, bad_receiver) == ERR_INVALID_DATA);
		Vector<int> bad_target = valid;
		bad_target.write[3] = out_of_range_stack;
		CHECK(bytecode_verify_with_code(script, function, bad_target) == ERR_INVALID_DATA);

		for (int identity = 0; identity < 4; identity++) {
			Vector<int> bad_identity_index = valid;
			bad_identity_index.write[5 + identity] = function->get_global_names_count();
			CHECK(bytecode_verify_with_code(script, function, bad_identity_index) == ERR_INVALID_DATA);
		}

		Vector<int> negative_identity_index = valid;
		negative_identity_index.write[5] = -1;
		CHECK(bytecode_verify_with_code(script, function, negative_identity_index) == ERR_INVALID_DATA);

		Vector<int> bad_call_kind = valid;
		bad_call_kind.write[9] = 2;
		CHECK(bytecode_verify_with_code(script, function, bad_call_kind) == ERR_INVALID_DATA);

		memdelete(function);

		// A table index can be in range while referring to an empty StringName. Such an identity can
		// never resolve and must be rejected at load time instead of reaching the VM's runtime error.
		for (int empty_identity = 0; empty_identity < 4; empty_identity++) {
			StringName identity_names[] = {
				SNAME("res://enum_owner.fs"),
				SNAME("Owner"),
				SNAME("Status"),
				SNAME("resolve"),
			};
			identity_names[empty_identity] = StringName();

			FSByteCodeGenerator empty_generator;
			empty_generator.write_start(script.ptr(),
					vformat("empty_enum_identity_%d_%d", (int)opcode_case.opcode, empty_identity),
					true, Variant(), FSDataType());
			const uint32_t empty_receiver_index = empty_generator.add_or_get_constant(1);
			const FSCodeGenerator::Address empty_base(
					FSCodeGenerator::Address::CONSTANT, empty_receiver_index);
			FSCodeGenerator::Address empty_target;
			if (opcode_case.has_target) {
				const uint32_t empty_target_index = empty_generator.add_temporary(FSDataType());
				empty_target = FSCodeGenerator::Address(
						FSCodeGenerator::Address::TEMPORARY, empty_target_index);
			}
			empty_generator.write_enum_call(empty_target, empty_base, Vector<FSCodeGenerator::Address>(),
					identity_names[0], identity_names[1], identity_names[2], identity_names[3],
					false, opcode_case.is_async);
			if (opcode_case.has_target) {
				empty_generator.pop_temporary();
			}
			FSFunction *empty_function = empty_generator.write_end();
			REQUIRE(empty_function != nullptr);
			CHECK(bytecode_verify_with_code(script, empty_function, empty_function->get_code()) == ERR_INVALID_DATA);
			memdelete(empty_function);
		}
	}
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier rejects out-of-range operand addresses") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func measure(text: String) -> int:\n"
			"\treturn text.length()\n");
	FSFunction *function = bytecode_round_trip_member_function(script, SNAME("measure"));

	const int stack_size = function->get_max_stack_size();
	REQUIRE(stack_size > FSFunction::ADDR_STACK_NIL);

	// A stack operand past the function's stack window is unchecked by the release VM.
	Vector<int> out_of_range_stack;
	out_of_range_stack.push_back(FSFunction::OPCODE_RETURN);
	out_of_range_stack.push_back(FSFunction::ADDR_TYPE_STACK << FSFunction::ADDR_BITS | stack_size);
	CHECK(bytecode_verify_with_code(script, function, out_of_range_stack) == ERR_INVALID_DATA);

	// A member operand with no members addressable is likewise rejected.
	Vector<int> out_of_range_member;
	out_of_range_member.push_back(FSFunction::OPCODE_RETURN);
	out_of_range_member.push_back(FSFunction::ADDR_TYPE_MEMBER << FSFunction::ADDR_BITS | 0x00ffffff);
	CHECK(bytecode_verify_with_code(script, function, out_of_range_member) == ERR_INVALID_DATA);

	// The same instruction with an in-range fixed stack address passes.
	Vector<int> in_range;
	in_range.push_back(FSFunction::OPCODE_RETURN);
	in_range.push_back(FSFunction::ADDR_SELF);
	CHECK(bytecode_verify_with_code(script, function, in_range) == OK);

	bytecode_destroy_restored_function(script, function);
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier rejects malformed jumps and truncated instructions") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func measure(text: String) -> int:\n"
			"\treturn text.length()\n");
	FSFunction *function = bytecode_round_trip_member_function(script, SNAME("measure"));

	// A jump past the end of the code.
	Vector<int> jump_past_end;
	jump_past_end.push_back(FSFunction::OPCODE_JUMP);
	jump_past_end.push_back(9999);
	CHECK(bytecode_verify_with_code(script, function, jump_past_end) == ERR_INVALID_DATA);

	// A jump into the middle of an instruction (offset 1 is this jump's operand, not a boundary).
	Vector<int> jump_into_instruction;
	jump_into_instruction.push_back(FSFunction::OPCODE_JUMP);
	jump_into_instruction.push_back(1);
	CHECK(bytecode_verify_with_code(script, function, jump_into_instruction) == ERR_INVALID_DATA);

	// A jump to exactly one-past-end (`code_size`) is rejected: the release VM dispatches
	// `_code_ptr[ip]` with no `ip < code_size` guard, so landing there reads an opcode past the buffer.
	// Valid loop-exit jumps land on the real instruction after the loop (at minimum the trailing END),
	// never at `code_size`.
	Vector<int> jump_to_end;
	jump_to_end.push_back(FSFunction::OPCODE_JUMP);
	jump_to_end.push_back(2);
	CHECK(bytecode_verify_with_code(script, function, jump_to_end) == ERR_INVALID_DATA);

	// A forward jump onto a real instruction boundary strictly inside the code passes: the jump lands
	// on the trailing terminator, which also satisfies the end-in-a-terminator requirement.
	Vector<int> jump_to_terminator;
	jump_to_terminator.push_back(FSFunction::OPCODE_JUMP);
	jump_to_terminator.push_back(2);
	jump_to_terminator.push_back(FSFunction::OPCODE_END);
	CHECK(bytecode_verify_with_code(script, function, jump_to_terminator) == OK);

	// An instruction whose operands run past the end of the code.
	Vector<int> truncated_instruction;
	truncated_instruction.push_back(FSFunction::OPCODE_JUMP); // Expects a following target word.
	CHECK(bytecode_verify_with_code(script, function, truncated_instruction) == ERR_INVALID_DATA);

	// An unknown opcode byte.
	Vector<int> unknown_opcode;
	unknown_opcode.push_back(FSFunction::OPCODE_END + 1000);
	CHECK(bytecode_verify_with_code(script, function, unknown_opcode) == ERR_INVALID_DATA);

	bytecode_destroy_restored_function(script, function);
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier rejects overflowing variable-argument counts") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func measure(text: String) -> int:\n"
			"\treturn text.length()\n");
	FSFunction *function = bytecode_round_trip_member_function(script, SNAME("measure"));

	// The instruction-argument count of a variable-argument opcode is a raw, attacker-controlled code
	// word. A value near INT_MAX must be rejected outright: it must never overflow the signed
	// `ip + length` overrun guard into a negative (which would slip past the check straight into a
	// multi-billion-iteration argument loop and negative-offset reads). A plain byte flip cannot
	// synthesize such a value, so this uses explicit large counts across several var-arg opcodes.
	static const int large_counts[] = { 0x7ffffff0, 0x7fffffff, 0x40000000, 1000 };
	static const int vararg_opcodes[] = {
		FSFunction::OPCODE_CALL,
		FSFunction::OPCODE_CONSTRUCT,
		FSFunction::OPCODE_CONSTRUCT_TYPED_DICTIONARY,
		FSFunction::OPCODE_CALL_BUILTIN_STATIC,
		FSFunction::OPCODE_CREATE_LAMBDA,
	};
	for (int vararg_opcode : vararg_opcodes) {
		for (int large_count : large_counts) {
			Vector<int> overflowing;
			overflowing.push_back(vararg_opcode);
			overflowing.push_back(large_count);
			CHECK(bytecode_verify_with_code(script, function, overflowing) == ERR_INVALID_DATA);
		}
	}

	bytecode_destroy_restored_function(script, function);
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier checks the typed-dictionary value type-info operand") {
	// A typed dictionary opcode carries two packed type-info addresses (key at ip+3, value at ip+4).
	// The value operand is the easy one to miss, and the release VM dereferences it, so a crafted
	// out-of-range ip+4 must be rejected. `print(...)` guarantees a non-empty global-name table so the
	// accept case can name a valid global index.
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func run() -> void:\n"
			"\tprint(1)\n");
	FSFunction *function = bytecode_round_trip_member_function(script, SNAME("run"));

	const int stack_size = function->get_max_stack_size();
	REQUIRE(stack_size > FSFunction::ADDR_STACK_NIL);
	const int global_names_count = function->get_global_names_count();
	REQUIRE(global_names_count > 0);

	const int out_of_range_stack = FSFunction::ADDR_TYPE_STACK << FSFunction::ADDR_BITS | stack_size;

	// Layout: [op, dst, value, key_type_info, value_type_info, key_builtin, key_global, value_builtin,
	// value_global]. Only the value_type_info address at ip+4 is out of range.
	Vector<int> bad_value_type_info;
	bad_value_type_info.push_back(FSFunction::OPCODE_TYPE_TEST_DICTIONARY);
	bad_value_type_info.push_back(FSFunction::ADDR_SELF);
	bad_value_type_info.push_back(FSFunction::ADDR_SELF);
	bad_value_type_info.push_back(FSFunction::ADDR_SELF);
	bad_value_type_info.push_back(out_of_range_stack);
	bad_value_type_info.push_back(0);
	bad_value_type_info.push_back(0);
	bad_value_type_info.push_back(0);
	bad_value_type_info.push_back(0);
	CHECK(bytecode_verify_with_code(script, function, bad_value_type_info) == ERR_INVALID_DATA);

	// The same instruction with every operand in range passes, so the check is not over-tight. A
	// trailing OPCODE_END terminates the function, which the verifier now requires.
	Vector<int> valid_typed_dictionary;
	valid_typed_dictionary.push_back(FSFunction::OPCODE_TYPE_TEST_DICTIONARY);
	valid_typed_dictionary.push_back(FSFunction::ADDR_SELF);
	valid_typed_dictionary.push_back(FSFunction::ADDR_SELF);
	valid_typed_dictionary.push_back(FSFunction::ADDR_SELF);
	valid_typed_dictionary.push_back(FSFunction::ADDR_SELF);
	valid_typed_dictionary.push_back(0);
	valid_typed_dictionary.push_back(0);
	valid_typed_dictionary.push_back(0);
	valid_typed_dictionary.push_back(0);
	valid_typed_dictionary.push_back(FSFunction::OPCODE_END);
	CHECK(bytecode_verify_with_code(script, function, valid_typed_dictionary) == OK);

	bytecode_destroy_restored_function(script, function);
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier rejects out-of-range table indices") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func measure(text: String) -> int:\n"
			"\treturn text.length()\n");
	FSFunction *function = bytecode_round_trip_member_function(script, SNAME("measure"));

	const int global_names_count = function->get_global_names_count();

	// OPCODE_GET_MEMBER reads a global-name index at offset 2; an index equal to the table size is
	// one past the end.
	Vector<int> bad_global_name;
	bad_global_name.push_back(FSFunction::OPCODE_GET_MEMBER);
	bad_global_name.push_back(FSFunction::ADDR_SELF);
	bad_global_name.push_back(global_names_count);
	CHECK(bytecode_verify_with_code(script, function, bad_global_name) == ERR_INVALID_DATA);

	if (global_names_count > 0) {
		Vector<int> good_global_name;
		good_global_name.push_back(FSFunction::OPCODE_GET_MEMBER);
		good_global_name.push_back(FSFunction::ADDR_SELF);
		good_global_name.push_back(0);
		good_global_name.push_back(FSFunction::OPCODE_END);
		CHECK(bytecode_verify_with_code(script, function, good_global_name) == OK);
	}

	bytecode_destroy_restored_function(script, function);
}

// Byte offsets of the fixed-width function-header fields inside a standalone function payload, in the
// exact order `FSBytecodeExporter::serialize_function` writes them: a u32 name-table index, a u8 flag
// byte, then the five int32 fields. Tests patch these to synthesize otherwise unreachable metadata.
enum FunctionPayloadOffset {
	FUNCTION_PAYLOAD_ARGUMENT_COUNT = 9,
	FUNCTION_PAYLOAD_VARARG_INDEX = 13,
	FUNCTION_PAYLOAD_STACK_SIZE = 17,
	FUNCTION_PAYLOAD_INSTRUCTION_ARGS_SIZE = 21,
};

// Overwrites a little-endian int32 field in a serialized function payload.
static void bytecode_patch_function_int32(Vector<uint8_t> &r_payload, int p_offset, int32_t p_value) {
	REQUIRE(p_offset + 4 <= r_payload.size());
	for (int i = 0; i < 4; i++) {
		r_payload.write[p_offset + i] = (uint8_t)(((uint32_t)p_value >> (i * 8)) & 0xFF);
	}
}

// Re-reads a (possibly tampered) standalone function payload through a fresh loader carrying the
// exporter's string table, and reports the loader result. Any function produced on success is
// released; its destructor's unregistration is identity-checked, so it never disturbs the original
// compiled function that owns the same name in the script.
static Error bytecode_read_function_payload(FSBytecodeExporter &r_exporter, const Ref<FoundryScript> &p_script,
		const Vector<uint8_t> &p_payload) {
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader = bytecode_loader_for(r_exporter, &resolver);
	Ref<StreamPeerBuffer> stream;
	stream.instantiate();
	stream->set_data_array(p_payload);
	FSFunction *function = nullptr;
	Vector<FSBytecodeLoader::LoadedLambdaInfo> lambda_info;
	const Error error = loader.read_function(stream.ptr(), p_script.ptr(), function, &lambda_info);
	if (function != nullptr) {
		memdelete(function);
	}
	return error;
}

TEST_CASE("[FoundryScript][BytecodeHardening] Loader bounds instruction-argument scratch size against opcode counts") {
	// `Vector2(x, y)` compiles to a construct opcode carrying two instruction arguments, so the
	// function's `_instruction_args_size` (the max instruction-argument count over its instructions)
	// is at least two. The release VM sizes its `instruction_args` scratch array from that field and
	// writes one pointer per bytecode-supplied count, so a payload whose field is smaller than a live
	// opcode's count would overrun (or, at zero, null-dereference) that array.
	// The arguments are runtime values, not literals, so the construct is not constant-folded away and
	// really loads two instruction arguments at call time.
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func build(x: float, y: float) -> Vector2:\n"
			"\treturn Vector2(x, y)\n");
	const HashMap<StringName, FSFunction *>::ConstIterator element = script->get_member_functions().find(SNAME("build"));
	REQUIRE(element);
	const int instruction_args_size = element->value->get_instruction_args_size();
	REQUIRE(instruction_args_size >= 1);

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, element->value);

	// The untouched payload, whose largest opcode uses exactly `_instruction_args_size` arguments,
	// loads cleanly: the bound is not over-tight.
	CHECK(bytecode_read_function_payload(exporter, script, payload) == OK);

	ERR_PRINT_OFF;
	// One short of the real maximum: the widest construct opcode now exceeds the scratch array.
	Vector<uint8_t> undersized = payload;
	bytecode_patch_function_int32(undersized, FUNCTION_PAYLOAD_INSTRUCTION_ARGS_SIZE, instruction_args_size - 1);
	CHECK(bytecode_read_function_payload(exporter, script, undersized) == ERR_INVALID_DATA);

	// Zero scratch entries while a live opcode still supplies arguments: the null-dereference case.
	Vector<uint8_t> zeroed = payload;
	bytecode_patch_function_int32(zeroed, FUNCTION_PAYLOAD_INSTRUCTION_ARGS_SIZE, 0);
	CHECK(bytecode_read_function_payload(exporter, script, zeroed) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][BytecodeHardening] Loader bounds argument and vararg slots against the stack size") {
	// `FSFunction::call()` writes incoming arguments at `stack[i + FIXED_ADDRESSES_MAX]` and a vararg
	// array at `stack[_vararg_index]`, all inside a stack sized by `_stack_size`. The release VM never
	// bounds-checks these writes, so a payload whose `_stack_size` cannot hold the declared arguments,
	// or whose `_vararg_index` points outside the stack, must be rejected at load time.
	//
	// The parameter is never referenced in the body, so its argument stack slot never appears as an
	// operand in the opcode stream. That isolates the argument-slot bound: shrinking `_stack_size`
	// below the argument slots is caught by this loader check alone, not incidentally by the verifier's
	// operand-address bound or the temporary-slot range check (there are no such operands or slots).
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func ignore(value: int) -> void:\n"
			"\tpass\n");
	const HashMap<StringName, FSFunction *>::ConstIterator element = script->get_member_functions().find(SNAME("ignore"));
	REQUIRE(element);
	const int argument_count = element->value->get_argument_count();
	const int stack_size = element->value->get_max_stack_size();
	REQUIRE(argument_count >= 1);
	REQUIRE(stack_size > FSFunction::FIXED_ADDRESSES_MAX);

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, element->value);

	// The untouched payload loads cleanly, so none of the added bounds are over-tight.
	CHECK(bytecode_read_function_payload(exporter, script, payload) == OK);

	ERR_PRINT_OFF;
	// A stack that holds the fixed slots but not the declared argument slots.
	Vector<uint8_t> no_room_for_arguments = payload;
	bytecode_patch_function_int32(no_room_for_arguments, FUNCTION_PAYLOAD_STACK_SIZE, FSFunction::FIXED_ADDRESSES_MAX);
	CHECK(bytecode_read_function_payload(exporter, script, no_room_for_arguments) == ERR_INVALID_DATA);

	// A vararg slot at exactly the stack size is one past the last addressable slot.
	Vector<uint8_t> vararg_out_of_range = payload;
	bytecode_patch_function_int32(vararg_out_of_range, FUNCTION_PAYLOAD_VARARG_INDEX, stack_size);
	CHECK(bytecode_read_function_payload(exporter, script, vararg_out_of_range) == ERR_INVALID_DATA);

	// A vararg slot far past the stack window.
	Vector<uint8_t> vararg_wild = payload;
	bytecode_patch_function_int32(vararg_wild, FUNCTION_PAYLOAD_VARARG_INDEX, stack_size + 4096);
	CHECK(bytecode_read_function_payload(exporter, script, vararg_wild) == ERR_INVALID_DATA);
	ERR_PRINT_ON;

	// A vararg slot at the last in-range stack index is accepted, proving the check stops at the
	// real boundary rather than rejecting every vararg slot.
	Vector<uint8_t> vararg_in_range = payload;
	bytecode_patch_function_int32(vararg_in_range, FUNCTION_PAYLOAD_VARARG_INDEX, stack_size - 1);
	CHECK(bytecode_read_function_payload(exporter, script, vararg_in_range) == OK);
}

// Attempts to load a buffer that is expected to be malformed, on a throwaway script, asserting only
// that the loader returns a clean result and never leaves a half-valid script behind (a crash would
// take down the test process, which is the property under test).
static void bytecode_attempt_hostile_load(const Vector<uint8_t> &p_buffer, const String &p_path) {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> target;
	target.instantiate();
	target->set_path_cache(p_path);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	loader.load_skeleton(p_buffer, target);
	const Error full_error = loader.load_full(p_buffer, target);
	// Either the load failed cleanly, or it happened to stay structurally valid and produced a valid
	// script. It must never yield an "OK but not valid" state.
	if (full_error == OK) {
		CHECK(target->is_valid());
	}
}

TEST_CASE("[FoundryScript][BytecodeHardening] Corrupt and truncated buffers never crash the loader") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"class Inner:\n"
			"\tvar value: int = 3\n"
			"\tfunc doubled() -> int:\n"
			"\t\treturn value * 2\n"
			"\n"
			"@export var speed: float = 1.5\n"
			"var counter: int = 0\n"
			"\n"
			"func step(amount: int) -> int:\n"
			"\tcounter += amount\n"
			"\tfor i in range(amount):\n"
			"\t\tcounter += i\n"
			"\treturn Inner.new().doubled() + counter\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);
	REQUIRE(buffer.size() > 16);
	const String path = original->get_script_path();

	ERR_PRINT_OFF;

	// Truncation at every length from empty to the full buffer. This crosses every section boundary
	// on the way, so no section can assume the following bytes exist.
	for (int length = 0; length < buffer.size(); length++) {
		Vector<uint8_t> truncated = buffer;
		truncated.resize(length);
		bytecode_attempt_hostile_load(truncated, path);
	}

	// A deterministic single-byte corruption schedule: at every offset, overwrite with a fixed set of
	// adversarial values and, separately, flip the low bit. No RNG is used, so the schedule is fully
	// reproducible.
	static const uint8_t adversarial_values[] = { 0x00, 0x01, 0x7f, 0x80, 0xff };
	for (int offset = 0; offset < buffer.size(); offset++) {
		const uint8_t original_byte = buffer[offset];
		for (uint8_t value : adversarial_values) {
			if (value == original_byte) {
				continue;
			}
			Vector<uint8_t> corrupted = buffer;
			corrupted.write[offset] = value;
			bytecode_attempt_hostile_load(corrupted, path);
		}
		Vector<uint8_t> bit_flipped = buffer;
		bit_flipped.write[offset] = original_byte ^ 0x01;
		bytecode_attempt_hostile_load(bit_flipped, path);
	}

	ERR_PRINT_ON;

	// The pristine buffer still loads cleanly after all the tampering (the loader kept no state).
	BytecodeTestResolver resolver;
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(path);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	CHECK(restored->is_valid());
}

TEST_CASE("[FoundryScript][BytecodeHardening] Serialized bytecode leaks no source-only identifiers") {
	// The source deliberately carries several distinctive tokens: a local variable, a comment, and a
	// distinctive string in a local's initializer. None of those may survive into the serialized
	// bytes. A member and a method name are recorded because the VM dispatches by name at runtime, so
	// they document the accepted residual surface. IMPORTANT: `hidden_parameter` is a parameter name,
	// which IS retained inside `MethodInfo` by design, so it is never asserted absent here.
	const String source =
			"var member_marker: int = 5 # DISTINCTIVE_TRAILING_COMMENT_MARKER\n"
			"\n"
			"func method_marker(hidden_parameter: int) -> int:\n"
			"\tvar UNIQUE_LOCAL_VARIABLE_MARKER := hidden_parameter + 1\n"
			"\tvar note := \"DISTINCTIVE_STRUCTURE_STRING_MARKER\"\n"
			"\treturn UNIQUE_LOCAL_VARIABLE_MARKER + note.length()\n";
	const Ref<FoundryScript> script = compile_bytecode_test_source(source);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(script, buffer) == OK);

	// Source-only markers must be absent from the serialized graph.
	CHECK(!bytecode_buffer_contains(buffer, "UNIQUE_LOCAL_VARIABLE_MARKER"));
	CHECK(!bytecode_buffer_contains(buffer, "DISTINCTIVE_TRAILING_COMMENT_MARKER"));
	CHECK(!bytecode_buffer_contains(buffer, source));

	// The member name and method name are name-dispatched at runtime, so they are the documented
	// residual surface and are expected to be present.
	CHECK(bytecode_buffer_contains(buffer, "member_marker"));
	CHECK(bytecode_buffer_contains(buffer, "method_marker"));
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier rejects fall-through and one-past-end targets") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func measure(text: String) -> int:\n"
			"\treturn text.length()\n");
	FSFunction *function = bytecode_round_trip_member_function(script, SNAME("measure"));

	// A jump whose target is exactly `code_size`. The release VM would set ip = code_size and dispatch
	// _code_ptr[code_size], reading an opcode past the buffer, so the target must be rejected even
	// though the stream ends in a terminator.
	Vector<int> jump_to_code_size;
	jump_to_code_size.push_back(FSFunction::OPCODE_JUMP);
	jump_to_code_size.push_back(3); // One past the end: code_size is 3 after the terminator below.
	jump_to_code_size.push_back(FSFunction::OPCODE_END);
	CHECK(bytecode_verify_with_code(script, function, jump_to_code_size) == ERR_INVALID_DATA);

	// A function whose last decoded instruction is not a terminator. The stream ends on an instruction
	// boundary, but execution would advance ip to code_size and read past the buffer, so it must be
	// rejected.
	Vector<int> non_terminator_tail;
	non_terminator_tail.push_back(FSFunction::OPCODE_ASSIGN);
	non_terminator_tail.push_back(FSFunction::ADDR_SELF);
	non_terminator_tail.push_back(FSFunction::ADDR_SELF);
	CHECK(bytecode_verify_with_code(script, function, non_terminator_tail) == ERR_INVALID_DATA);

	// The same body followed by a terminator is accepted, proving neither check is over-tight.
	Vector<int> terminated_tail;
	terminated_tail.push_back(FSFunction::OPCODE_ASSIGN);
	terminated_tail.push_back(FSFunction::ADDR_SELF);
	terminated_tail.push_back(FSFunction::ADDR_SELF);
	terminated_tail.push_back(FSFunction::OPCODE_END);
	CHECK(bytecode_verify_with_code(script, function, terminated_tail) == OK);

	bytecode_destroy_restored_function(script, function);
}

TEST_CASE("[FoundryScript][BytecodeHardening] Executed operator inline cache is zeroed on export") {
	// An untyped `a + b` lowers to the non-validated OPCODE_OPERATOR, which the VM patches in place on
	// first execution with inline-cache words: an operand signature, a cached return type, and a raw
	// validated-evaluator function pointer split across ints. Those words are process-local and must
	// never reach a `.fsb`; the exporter zeroes them back to the never-executed layout.
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func add(a, b):\n"
			"\treturn a + b\n");
	const HashMap<StringName, FSFunction *>::ConstIterator element = script->get_member_functions().find(SNAME("add"));
	REQUIRE(element);
	FSFunction *function = element->value;

	const Vector<int> &operator_offsets = function->export_fixups.operator_cache_offsets;
	REQUIRE(operator_offsets.size() == 1);
	const int operator_offset = operator_offsets[0];
	REQUIRE(function->get_code()[operator_offset] == FSFunction::OPCODE_OPERATOR);

	constexpr int operator_pointer_size = sizeof(Variant::ValidatedOperatorEvaluator) / sizeof(int);
	const int first_cache_word = operator_offset + 5;
	const int last_cache_word = operator_offset + 6 + operator_pointer_size;

	// Executing the function once populates the inline cache in place.
	Vector<Variant> arguments;
	arguments.push_back((int64_t)2);
	arguments.push_back((int64_t)3);
	const Variant original_result = bytecode_call_function(function, arguments);
	CHECK((int64_t)original_result == 5);

	// The signature word is now non-zero: the executed function really baked cache state, which is the
	// exact leak the exporter must strip.
	CHECK(function->get_code()[first_cache_word] != 0);

	// Serialize the executed function, read it back, and confirm every cache word is zero while the
	// base operands and operator enum survive.
	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, function);
	FSFunction *restored = bytecode_deserialize_function(exporter, payload, script);
	const Vector<int> &restored_code = restored->get_code();
	for (int offset = first_cache_word; offset <= last_cache_word; offset++) {
		CAPTURE(offset);
		CHECK(restored_code[offset] == 0);
	}
	CHECK(restored_code[operator_offset] == FSFunction::OPCODE_OPERATOR);
	CHECK(restored_code[operator_offset + 4] == function->get_code()[operator_offset + 4]);

	// The round-tripped function re-heals its cache on first run and computes the same result.
	const Variant restored_result = bytecode_call_function(restored, arguments);
	CHECK((int64_t)restored_result == 5);
	const Vector<int> &restored_operator_offsets =
			restored->export_fixups.operator_cache_offsets;
	REQUIRE_EQ(restored_operator_offsets.size(), 1);
	if (restored_operator_offsets.size() == 1) {
		CHECK_EQ(restored_operator_offsets[0], operator_offset);
	}

	// A loaded function must retain the symbolic cache descriptor too. Otherwise exporting it after
	// execution would persist the process-local signature, return type, and raw evaluator pointer.
	const Vector<uint8_t> restored_payload =
			bytecode_serialize_function_payload(exporter, restored);
	CHECK_EQ(restored_payload, payload);

	bytecode_destroy_restored_function(script, restored);
}

TEST_CASE("[FoundryScript][BytecodeHardening] finish() survives cascaded base/subclass destruction") {
	// A prior UAF in FSLanguage::finish() (fixed by pinning every listed script in a strong reference
	// before breaking cross-script references) surfaced when destroying one script cascaded into its
	// subclasses mid-sweep. Build a base and a subclass so that destruction cascades, then run a
	// finish()/init() cycle and confirm the language stays usable afterwards.
	compile_bytecode_test_source(
			"class Base:\n"
			"\tvar base_value: int = 1\n"
			"\n"
			"class Derived extends Base:\n"
			"\tvar derived_value: int = 2\n"
			"\tfunc total() -> int:\n"
			"\t\treturn base_value + derived_value\n");

	FSLanguage::get_singleton()->finish();
	FSLanguage::get_singleton()->init();

	// The language re-initialized cleanly and can compile again; a surviving dangling script would
	// have crashed during the finish() sweep above.
	const Ref<FoundryScript> after = compile_bytecode_test_source(
			"static func ping() -> int:\n"
			"\treturn 1\n");
	CHECK(after->get_member_functions().has(SNAME("ping")));
}

TEST_CASE("[FoundryScript][BytecodeHardening] Verifier rejects out-of-range builtin-static types") {
	// `print(1)` gives the host function a non-empty global-name table and an instruction-argument
	// scratch size of at least one, so the crafted CALL_BUILTIN_STATIC below can name a valid method
	// index and carry one instruction argument.
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func run() -> void:\n"
			"\tprint(1)\n");
	FSFunction *function = bytecode_round_trip_member_function(script, SNAME("run"));

	REQUIRE(function->get_global_names_count() > 0);
	REQUIRE(function->get_instruction_args_size() >= 1);

	// Layout of a CALL_BUILTIN_STATIC with one instruction argument: the argument sits at ip+2, then the
	// VM reads the builtin type at shift+1, the method-name global index at shift+2, and the argument
	// count at shift+3, where shift = ip + 1 + instruction_arg_count. `Variant::call_static` indexes
	// `builtin_method_info[type]` with no bounds check in a release build, so an out-of-range type must
	// be rejected here. The argument count is zero, so the only instruction-argument slot used is the
	// return slot at index 0, which stays inside the single-entry scratch array.
	const auto build_call = [](int p_builtin_type) {
		Vector<int> code;
		code.push_back(FSFunction::OPCODE_CALL_BUILTIN_STATIC);
		code.push_back(1); // instruction_arg_count
		code.push_back(FSFunction::ADDR_SELF); // instruction argument 0
		code.push_back(p_builtin_type); // builtin type at shift+1
		code.push_back(0); // method-name global index at shift+2
		code.push_back(0); // argument count at shift+3
		code.push_back(FSFunction::OPCODE_END);
		return code;
	};

	// A type past the end of the per-type tables, far past it, and a negative type are all rejected.
	CHECK(bytecode_verify_with_code(script, function, build_call(Variant::VARIANT_MAX)) == ERR_INVALID_DATA);
	CHECK(bytecode_verify_with_code(script, function, build_call(Variant::VARIANT_MAX + 4096)) == ERR_INVALID_DATA);
	CHECK(bytecode_verify_with_code(script, function, build_call(-1)) == ERR_INVALID_DATA);

	// A real builtin type and the last valid type both pass, proving the bound stops at the real
	// boundary rather than rejecting every builtin-static call.
	CHECK(bytecode_verify_with_code(script, function, build_call(Variant::VECTOR2)) == OK);
	CHECK(bytecode_verify_with_code(script, function, build_call(Variant::VARIANT_MAX - 1)) == OK);

	bytecode_destroy_restored_function(script, function);
}

TEST_CASE("[FoundryScript][BytecodeHardening] Duplicate member names in corrupt buffers fail cleanly") {
	// Two builtin-typed instance members of equal-length names so the string-table entry can be
	// byte-patched to collide. Each instance sizes its `members` array from the deduplicated
	// `member_indices` map, and `FSInstance::set`/`get` index that array by the stored member index.
	//
	// The members are left uninitialized so the implicit constructor emits no member-store opcode for
	// them: that keeps the collapsed member slot out of the bytecode the verifier scans, isolating the
	// duplicate-name rejection to the loader. The corrupted member is instead reached through the
	// external `Object::set`/`get` property path, which the bytecode verifier does not cover.
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"var alpha\n"
			"var bravo\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// The pristine buffer round-trips: two distinct members, each default-initialized and independently
	// set/get, so the added bounds are not over-tight and the per-instance member array holds both slots.
	{
		BytecodeTestResolver resolver;
		Ref<FoundryScript> accepted;
		accepted.instantiate();
		accepted->set_path_cache(original->get_script_path());
		FSBytecodeLoader loader;
		loader.set_resolver(&resolver);
		REQUIRE(loader.load_full(buffer, accepted) == OK);
		REQUIRE(accepted->is_valid());
		{
			const Variant instance_variant = bytecode_new_instance(accepted);
			Object *instance = instance_variant;
			// Both uninitialized `int` members default to zero and are then independently written and
			// read back through the external property path, exercising the exact `members[index]` access
			// the duplicate-name rejection protects.
			CHECK((int64_t)instance->get(SNAME("alpha")) == 0);
			CHECK((int64_t)instance->get(SNAME("bravo")) == 0);
			instance->set(SNAME("alpha"), 33);
			instance->set(SNAME("bravo"), 44);
			CHECK((int64_t)instance->get(SNAME("alpha")) == 33);
			CHECK((int64_t)instance->get(SNAME("bravo")) == 44);
		}
		accepted->clear();
	}

	// Rename the second member to the first one's name in the string table (same length), so the loader
	// reads two member entries both named "alpha". The second entry keeps its own index of 1, which is
	// `< member_count` (2) but past the deduplicated per-instance array (size 1): without the
	// duplicate-name rejection this is an out-of-bounds `members` access in `FSInstance::set`/`get`.
	const CharString marker = String("bravo").utf8();
	const CharString replacement = String("alpha").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= buffer.size(); i++) {
		if (memcmp(&buffer[i], marker.get_data(), marker.length()) == 0) {
			memcpy(&buffer.write[i], replacement.get_data(), replacement.length());
			patched = true;
			break;
		}
	}
	REQUIRE(patched);

	Ref<FoundryScript> target;
	target.instantiate();
	target->set_path_cache(original->get_script_path());
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	ERR_PRINT_OFF;
	CHECK(loader.load_full(buffer, target) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
	CHECK(!target->is_valid());

	original->clear();
}

} // namespace FSTests

#endif // TOOLS_ENABLED
