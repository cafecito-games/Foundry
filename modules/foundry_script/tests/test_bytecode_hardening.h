/**************************************************************************/
/*  test_bytecode_hardening.h                                             */
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
	CHECK(FSBytecodeFormat::FORMAT_VERSION == 1);
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

	// A jump to exactly one-past-end is the valid loop-exit target.
	Vector<int> jump_to_end;
	jump_to_end.push_back(FSFunction::OPCODE_JUMP);
	jump_to_end.push_back(2);
	CHECK(bytecode_verify_with_code(script, function, jump_to_end) == OK);

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
		CHECK(bytecode_verify_with_code(script, function, good_global_name) == OK);
	}

	bytecode_destroy_restored_function(script, function);
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

} // namespace FSTests

#endif // TOOLS_ENABLED
