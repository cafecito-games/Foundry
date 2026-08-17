/**************************************************************************/
/*  test_specialized_return_bytecode.h                                    */
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

// Shared bytecode fixtures (in-process compile, test resolver). Every module test header is compiled
// into one generated test translation unit, so this include does not duplicate test registrations.
#include "test_bytecode_serialization.h"

#include "core/error/error_macros.h"

#include "tests/test_macros.h"

// A specialized return type is enforced against the returned value's reified arguments whenever the
// returned address's own static type does not already prove them, and a source that does prove them
// keeps the cheaper plain return. Both halves are asserted on the produced artifact: which return the
// function was lowered to, and what the lowered function does when it runs -- from the compiled source
// and from a `.fsb` reloaded with no front end behind it, since a shipped game only has the latter.

namespace FSTests {

struct SpecializedReturnErrorRecorder {
	SpecializedReturnErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~SpecializedReturnErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		SpecializedReturnErrorRecorder *self = static_cast<SpecializedReturnErrorRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	ErrorHandlerList handler;
	String messages;
};

// The opcode the function's single `return` statement was lowered to. Every function inspected here has
// exactly one statement, so the instruction that precedes the terminating `OPCODE_END` is that return.
// The two candidate layouts cannot be confused: the word a plain return would occupy in the typed
// layout is the type operand, a constant address whose `ADDR_TYPE_CONSTANT` bits put it far above any
// opcode value.
static int lowered_return_opcode(const FSFunction *p_function) {
	REQUIRE(p_function != nullptr);
	const Vector<int> &code = p_function->get_code();
	REQUIRE(code.size() >= 5);
	REQUIRE(code[code.size() - 1] == FSFunction::OPCODE_END);
	if (code[code.size() - 3] == FSFunction::OPCODE_RETURN) {
		return FSFunction::OPCODE_RETURN;
	}
	REQUIRE(code[code.size() - 5] == FSFunction::OPCODE_RETURN_TYPED_SCRIPT);
	return FSFunction::OPCODE_RETURN_TYPED_SCRIPT;
}

static const FSFunction *specialized_return_function(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, FSFunction *> &functions = p_script->get_member_functions();
	HashMap<StringName, FSFunction *>::ConstIterator found = functions.find(p_name);
	REQUIRE(found != functions.end());
	return found->value;
}

// Compiles the source, serializes it, then removes the on-disk script so nothing can re-parse it, and
// rebuilds the script from the bytes alone through the production skeleton/full load pair.
static Ref<FoundryScript> load_specialized_return_bytecode(const String &p_source,
		FSBytecodeExternalResolver *p_resolver, Ref<FoundryScript> *r_original) {
	const Ref<FoundryScript> original = compile_bytecode_test_source(p_source);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String script_path = original->get_script_path();
	*r_original = original;

	// The front end is unavailable from here on: the source the analyzer read no longer exists.
	REQUIRE(DirAccess::remove_absolute(script_path) == OK);
	REQUIRE_FALSE(FileAccess::exists(script_path));

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);

	FSBytecodeLoader loader;
	loader.set_resolver(p_resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	REQUIRE(restored->is_valid());
	CHECK(restored->is_compiled_binary());
	return restored;
}

static Object *instantiate_specialized_return_script(const Ref<FoundryScript> &p_script, Variant &r_owner) {
	Callable::CallError call_error;
	r_owner = p_script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = r_owner;
	REQUIRE(instance != nullptr);
	return instance;
}

static const char *specialized_return_source =
		"extends RefCounted\n"
		"\n"
		"class Pair[A, B]:\n"
		"\tpass\n"
		"\n"
		"class IntStringPair extends Pair[int, String]:\n"
		"\tpass\n"
		"\n"
		"trait Keeper[T]:\n"
		"\tfunc label() -> String:\n"
		"\t\treturn \"keeper\"\n"
		"\n"
		"class IntKeeper:\n"
		"\tuses Keeper[int]\n"
		"\n"
		"class StringKeeper:\n"
		"\tuses Keeper[String]\n"
		"\n"
		"func give_exact(value: Pair[int, String]) -> Pair[int, String]:\n"
		"\treturn value\n"
		"\n"
		"func give_unspecialized(value: Pair) -> Pair[int, String]:\n"
		"\treturn value\n"
		"\n"
		"func give_subclass(value: IntStringPair) -> Pair[int, String]:\n"
		"\treturn value\n"
		"\n"
		"func give_unspecialized_return(value: Pair) -> Pair:\n"
		"\treturn value\n"
		"\n"
		"func give_conformer(value: IntKeeper) -> Keeper[int]:\n"
		"\treturn value\n"
		"\n"
		"func give_gradual_conformer(value: Keeper) -> Keeper[int]:\n"
		"\treturn value\n"
		"\n"
		"func make_exact() -> Variant:\n"
		"\treturn Pair[int, String].new()\n"
		"\n"
		"func make_raw() -> Variant:\n"
		"\treturn Pair.new()\n"
		"\n"
		"func make_subclass() -> Variant:\n"
		"\treturn IntStringPair.new()\n"
		"\n"
		"func make_conflicting() -> Variant:\n"
		"\treturn Pair[int, Node].new()\n"
		"\n"
		"func make_int_keeper() -> Variant:\n"
		"\treturn IntKeeper.new()\n"
		"\n"
		"func make_string_keeper() -> Variant:\n"
		"\treturn StringKeeper.new()\n";

TEST_CASE("[FoundryScript][Bytecode][SpecializedReturn] A statically typed return is checked unless its type proves the specialization") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(specialized_return_source);

	// An exact source proves the whole declared contract, so it keeps the plain return.
	CHECK(lowered_return_opcode(specialized_return_function(script, SNAME("give_exact"))) == FSFunction::OPCODE_RETURN);
	// An unspecialized base and a subclass both prove the nominal half alone: the value's arguments are
	// still open, so the declared descriptor has to reach the runtime.
	CHECK(lowered_return_opcode(specialized_return_function(script, SNAME("give_unspecialized"))) == FSFunction::OPCODE_RETURN_TYPED_SCRIPT);
	CHECK(lowered_return_opcode(specialized_return_function(script, SNAME("give_subclass"))) == FSFunction::OPCODE_RETURN_TYPED_SCRIPT);
	// An unspecialized return type has no arguments to compare, so it is unaffected.
	CHECK(lowered_return_opcode(specialized_return_function(script, SNAME("give_unspecialized_return"))) == FSFunction::OPCODE_RETURN);
	// A conformer class is not the trait it conforms to, so a specialized trait return is checked too.
	CHECK(lowered_return_opcode(specialized_return_function(script, SNAME("give_conformer"))) == FSFunction::OPCODE_RETURN_TYPED_SCRIPT);
}

TEST_CASE("[FoundryScript][Bytecode][SpecializedReturn] The checked return keeps its arguments without a front end") {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> original;
	const Ref<FoundryScript> restored = load_specialized_return_bytecode(specialized_return_source, &resolver, &original);

	Variant original_owner;
	Object *original_instance = instantiate_specialized_return_script(original, original_owner);
	Variant restored_owner;
	Object *restored_instance = instantiate_specialized_return_script(restored, restored_owner);

	for (Object *instance : { original_instance, restored_instance }) {
		// Exact, absent and projected evidence are all accepted: the rule at the return is the gradual
		// one, so only evidence that contradicts the declaration rejects.
		CHECK(instance->call(SNAME("give_unspecialized"), instance->call(SNAME("make_exact"))).get_type() == Variant::OBJECT);
		CHECK(instance->call(SNAME("give_unspecialized"), instance->call(SNAME("make_raw"))).get_type() == Variant::OBJECT);
		CHECK(instance->call(SNAME("give_subclass"), instance->call(SNAME("make_subclass"))).get_type() == Variant::OBJECT);
		// A trait conformer reaches the trait through its declared `uses`, which a base-script chain walk
		// could never answer; the newly checked return must still accept it.
		CHECK(instance->call(SNAME("give_conformer"), instance->call(SNAME("make_int_keeper"))).get_type() == Variant::OBJECT);
		CHECK(instance->call(SNAME("give_gradual_conformer"), instance->call(SNAME("make_int_keeper"))).get_type() == Variant::OBJECT);
	}

	const auto collect_rejection = [](Object *p_instance, const char *p_giver, const char *p_maker) {
		SpecializedReturnErrorRecorder recorder;
		ERR_PRINT_OFF;
		const Variant returned = p_instance->call(StringName(p_giver), p_instance->call(StringName(p_maker)));
		ERR_PRINT_ON;
		CHECK(returned.get_type() == Variant::NIL);
		return recorder.messages;
	};

	// A contradicting specialization arriving through a statically typed but unspecialized source is
	// rejected, and the diagnostic names both sides with their arguments.
	const String original_conflict = collect_rejection(original_instance, "give_unspecialized", "make_conflicting");
	CHECK(original_conflict.contains("Pair[int, String]"));
	CHECK(original_conflict.contains("Pair[int, Node]"));

	// The restored script has no analyzer behind it, so this is what proves the declared arguments
	// reached the compiled function rather than being reconstructed by the front end.
	CHECK(collect_rejection(restored_instance, "give_unspecialized", "make_conflicting") == original_conflict);

	// The trait half rejects on the arguments the conformer declared, not on nominal conformance.
	const String original_trait_conflict = collect_rejection(original_instance, "give_gradual_conformer", "make_string_keeper");
	CHECK(original_trait_conflict.contains("Keeper[int]"));
	CHECK(original_trait_conflict.contains("StringKeeper"));
	CHECK(collect_rejection(restored_instance, "give_gradual_conformer", "make_string_keeper") == original_trait_conflict);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
