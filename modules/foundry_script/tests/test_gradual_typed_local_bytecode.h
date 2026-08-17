/**************************************************************************/
/*  test_gradual_typed_local_bytecode.h                                   */
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

// A specialized script-typed local reached through a gradual source is validated against the arguments
// the declaration spelled out, and the declaration has to survive into the compiled function: a shipped
// game runs from a `.fsb` with no parser or analyzer state, so a destination whose arguments were
// dropped at codegen would accept a contradicting value there while rejecting it from source. Every
// assertion below is on executed behavior -- a store accepted, a store rejected, and the reason given --
// rather than on the encoded bytes.

namespace FSTests {

struct GradualTypedLocalErrorRecorder {
	GradualTypedLocalErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~GradualTypedLocalErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		GradualTypedLocalErrorRecorder *self = static_cast<GradualTypedLocalErrorRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	void clear() { messages = String(); }

	ErrorHandlerList handler;
	String messages;
};

// Compiles the source, serializes it, then removes the on-disk script so nothing can re-parse it, and
// rebuilds the script from the bytes alone through the production skeleton/full load pair.
static Ref<FoundryScript> load_gradual_typed_local_bytecode(const String &p_source,
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

static Object *instantiate_gradual_typed_local_script(const Ref<FoundryScript> &p_script, Variant &r_owner) {
	Callable::CallError call_error;
	r_owner = p_script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = r_owner;
	REQUIRE(instance != nullptr);
	return instance;
}

TEST_CASE("[FoundryScript][Bytecode][GradualTypedLocal] A specialized local keeps its arguments without a front end") {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> original;
	const Ref<FoundryScript> restored = load_gradual_typed_local_bytecode(
			"extends RefCounted\n"
			"\n"
			"class Pair[A, B]:\n"
			"\tpass\n"
			"\n"
			"class IntStringPair extends Pair[int, String]:\n"
			"\tpass\n"
			"\n"
			"class ConflictingPair extends Pair[int, Node]:\n"
			"\tpass\n"
			"\n"
			"func supply(value: Variant) -> Variant:\n"
			"\treturn value\n"
			"\n"
			"func keep(value: Variant) -> bool:\n"
			"\tvar slot: Pair[int, String] = supply(value)\n"
			"\treturn slot != null\n"
			"\n"
			"func keep_unspecialized(value: Variant) -> bool:\n"
			"\tvar slot: Pair = supply(value)\n"
			"\treturn slot != null\n"
			"\n"
			"func make_exact() -> Variant:\n"
			"\treturn Pair[int, String].new()\n"
			"\n"
			"func make_raw() -> Variant:\n"
			"\treturn Pair.new()\n"
			"\n"
			"func make_projected() -> Variant:\n"
			"\treturn IntStringPair.new()\n"
			"\n"
			"func make_conflicting() -> Variant:\n"
			"\treturn Pair[int, Node].new()\n"
			"\n"
			"func make_conflicting_projection() -> Variant:\n"
			"\treturn ConflictingPair.new()\n",
			&resolver, &original);

	Variant original_owner;
	Object *original_instance = instantiate_gradual_typed_local_script(original, original_owner);
	Variant restored_owner;
	Object *restored_instance = instantiate_gradual_typed_local_script(restored, restored_owner);

	// Exact, absent and projected evidence are all accepted, from both the compiled source and the
	// bytecode: the store rule is gradual, so only evidence that contradicts the declaration rejects.
	const auto check_accepted = [](Object *p_instance, const char *p_maker) {
		CAPTURE(p_maker);
		const Variant value = p_instance->call(StringName(p_maker));
		REQUIRE(value.get_type() == Variant::OBJECT);
		CHECK(bool(p_instance->call(SNAME("keep"), value)));
	};
	for (Object *instance : { original_instance, restored_instance }) {
		check_accepted(instance, "make_exact");
		check_accepted(instance, "make_raw");
		check_accepted(instance, "make_projected");
		// The same conflicting value is a member of the unspecialized slot, so the rejection below is
		// the declared arguments talking and not a nominal failure.
		CHECK(bool(instance->call(SNAME("keep_unspecialized"), instance->call(SNAME("make_conflicting")))));
	}

	// A contradicting specialization is rejected, and the diagnostic names both sides with their
	// arguments rather than naming the bare class twice.
	const auto collect_rejection = [](Object *p_instance, const char *p_maker) {
		GradualTypedLocalErrorRecorder recorder;
		ERR_PRINT_OFF;
		CHECK_FALSE(bool(p_instance->call(SNAME("keep"), p_instance->call(StringName(p_maker)))));
		ERR_PRINT_ON;
		return recorder.messages;
	};

	const String original_conflict = collect_rejection(original_instance, "make_conflicting");
	CHECK(original_conflict.contains("Pair[int, String]"));
	CHECK(original_conflict.contains("Pair[int, Node]"));

	// The restored script has no analyzer behind it, so this is what proves the declared arguments
	// reached the compiled function rather than being reconstructed by the front end.
	const String restored_conflict = collect_rejection(restored_instance, "make_conflicting");
	CHECK(restored_conflict == original_conflict);

	// A subclass that fixes the base's second argument to something else proves the same conflict
	// through the projection table rather than through the instance's own arguments.
	const String original_projection = collect_rejection(original_instance, "make_conflicting_projection");
	CHECK(original_projection.contains("Pair[int, String]"));
	const String restored_projection = collect_rejection(restored_instance, "make_conflicting_projection");
	CHECK(restored_projection == original_projection);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
