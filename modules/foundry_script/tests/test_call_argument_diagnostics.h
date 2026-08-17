/**************************************************************************/
/*  test_call_argument_diagnostics.h                                      */
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

// A rejected dynamic call is reported through `Callable::CallError`, whose `expected` field can carry
// only a `Variant::Type`. Everything a declaration layers on top of that carrier -- an integer width,
// a specialization, a container element type, a class handle, a rest element type -- is invisible to
// it, which is why a width rejection used to read as an unexplained `int` to `int` conversion failure.
//
// The exact declaration is recovered from the callee's own compiled descriptor instead, so the
// messages below have to hold for a script rebuilt from serialized bytecode alone: a shipped game has
// no front end to reconstruct a signature from. The public error fields stay exactly as they were.

namespace FSTests {

struct CallArgumentDiagnosticRecorder {
	CallArgumentDiagnosticRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~CallArgumentDiagnosticRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		CallArgumentDiagnosticRecorder *self = static_cast<CallArgumentDiagnosticRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	ErrorHandlerList handler;
	String messages;
};

// Compiles the source, serializes it, then removes the on-disk script so nothing can re-parse it, and
// rebuilds the script from the bytes alone through the production skeleton/full load pair.
static Ref<FoundryScript> load_call_argument_diagnostic_bytecode(const String &p_source,
		FSBytecodeExternalResolver *p_resolver, Ref<FoundryScript> *r_original) {
	const Ref<FoundryScript> original = compile_bytecode_test_source(p_source);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String script_path = original->get_script_path();
	*r_original = original;

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

static Object *instantiate_call_argument_diagnostic_script(const Ref<FoundryScript> &p_script, Variant &r_owner) {
	Callable::CallError call_error;
	r_owner = p_script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = r_owner;
	REQUIRE(instance != nullptr);
	return instance;
}

// Runs one rejecting method and returns everything the script error handler saw. The rejection is
// expected, so the console printing is suppressed while the recorder still observes the message.
static String collect_call_argument_diagnostic(Object *p_instance, const char *p_method) {
	CAPTURE(p_method);
	CallArgumentDiagnosticRecorder recorder;
	ERR_PRINT_OFF;
	p_instance->call(StringName(p_method));
	ERR_PRINT_ON;
	return recorder.messages;
}

static const char *call_argument_diagnostic_source =
		"extends RefCounted\n"
		"\n"
		"class Pair[A, B]:\n"
		"\tpass\n"
		"\n"
		"func take_int(value: int) -> void:\n"
		"\tprint(value)\n"
		"\n"
		"func take_pair(value: Pair[int, String]) -> void:\n"
		"\tprint(value != null)\n"
		"\n"
		"func take_handle(value: Type[RefCounted]) -> void:\n"
		"\tprint(value != null)\n"
		"\n"
		"func take_rest(...values: Array[int]) -> void:\n"
		"\tprint(values)\n"
		"\n"
		"func take_two(first: int, second: Array[int]) -> void:\n"
		"\tprint(first, second)\n"
		"\n"
		"func reject_width() -> void:\n"
		"\tvar callback: Callable = take_int\n"
		"\tcallback.call(9223372036854775807)\n"
		"\n"
		"func reject_converted_width() -> void:\n"
		"\tvar callback: Callable = take_int\n"
		"\tcallback.call(2147483648.0)\n"
		"\n"
		"func reject_specialization() -> void:\n"
		"\tvar callback: Callable = take_pair\n"
		"\tcallback.call(Pair[int, Node].new())\n"
		"\n"
		"func reject_handle() -> void:\n"
		"\tvar callback: Callable = take_handle\n"
		"\tcallback.call(RefCounted.new())\n"
		"\n"
		"func reject_rest() -> void:\n"
		"\tvar callback: Callable = take_rest\n"
		"\tcallback.call(1, \"two\")\n"
		"\n"
		"func reject_bound() -> void:\n"
		"\tvar callback: Callable = take_two\n"
		"\tcallback.bind(\"bound\").call(1)\n"
		"\n"
		"func reject_captured_lambda() -> void:\n"
		"\tvar captured: String = \"kept\"\n"
		"\tvar callback := func(value: int) -> void:\n"
		"\t\tprint(captured, value)\n"
		"\tcallback.call([1])\n"
		"\n"
		"func reject_class_handle_argument() -> void:\n"
		"\tvar callback: Callable = take_int\n"
		"\tcallback.call(RefCounted)\n"
		"\n"
		"func reject_unbound() -> void:\n"
		"\tvar callback: Callable = take_int\n"
		"\tcallback.unbind(1).call([1], \"dropped\")\n";

TEST_CASE("[Modules][FoundryScript][CallArgumentDiagnostics] A rejected call argument names its exact declared parameter") {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> original;
	const Ref<FoundryScript> restored = load_call_argument_diagnostic_bytecode(
			call_argument_diagnostic_source, &resolver, &original);

	Variant original_owner;
	Object *original_instance = instantiate_call_argument_diagnostic_script(original, original_owner);
	Variant restored_owner;
	Object *restored_instance = instantiate_call_argument_diagnostic_script(restored, restored_owner);

	// A width rejection has to state the range failure. The carrier alone spells both sides `int`, so
	// the old wording claimed a conversion between a type and itself had failed.
	const String width = collect_call_argument_diagnostic(original_instance, "reject_width");
	CHECK(width.contains(R"(Argument 1 cannot be passed to parameter of type "int")"));
	CHECK(width.contains("value 9223372036854775807 is outside the declared range"));
	CHECK_FALSE(width.contains("Cannot convert argument 1 from int to int"));

	// A converted argument names the magnitude the parameter would have received, since truncation
	// toward zero happens before the width is asked about the result.
	const String converted_width = collect_call_argument_diagnostic(original_instance, "reject_converted_width");
	CHECK(converted_width.contains(R"(Argument 1 cannot be passed to parameter of type "int")"));
	CHECK(converted_width.contains("value 2147483648 is outside the declared range"));

	// Both specializations are named, which is the whole difference between the two types.
	const String specialization = collect_call_argument_diagnostic(original_instance, "reject_specialization");
	CHECK(specialization.contains(R"(Argument 1 has type "Pair[int, Node]")"));
	CHECK(specialization.contains(R"(the parameter requires "Pair[int, String]")"));

	// A class handle is an Object in carrier terms, so only the declaration can say that the call
	// wanted the class itself rather than an instance of it.
	const String handle = collect_call_argument_diagnostic(original_instance, "reject_handle");
	CHECK(handle.contains(R"(the parameter requires "Type[RefCounted]")"));

	// A rest failure names the element type the tail collects, at the absolute argument position.
	const String rest = collect_call_argument_diagnostic(original_instance, "reject_rest");
	CHECK(rest.contains("Argument 2 has type"));
	CHECK(rest.contains(R"(the rest parameter element requires "int")"));

	// A bound argument is counted where the callee counted it: after the arguments the call site
	// passed, not where the call site wrote it.
	const String bound = collect_call_argument_diagnostic(original_instance, "reject_bound");
	CHECK(bound.contains(R"(Argument 2 has type "String")"));
	CHECK(bound.contains(R"(the parameter requires "Array[int]")"));

	// An unbound callable drops its trailing arguments before the callee sees them, so the reported
	// position is the callee's own.
	const String unbound = collect_call_argument_diagnostic(original_instance, "reject_unbound");
	CHECK(unbound.contains(R"(Argument 1 has type "Array")"));
	CHECK(unbound.contains(R"(the parameter requires "int")"));

	// A lambda receives its captures as leading parameters and re-bases the reported index past them,
	// so the parameter named here is the lambda's own, not the capture that precedes it.
	const String captured_lambda = collect_call_argument_diagnostic(original_instance, "reject_captured_lambda");
	CHECK(captured_lambda.contains(R"(Argument 1 has type "Array")"));
	CHECK(captured_lambda.contains(R"(the parameter requires "int")"));
	CHECK_FALSE(captured_lambda.contains(R"(the parameter requires "String")"));

	// A class handle is a wrapper object, and the value is named by the class it denotes rather than
	// by the wrapper's implementation class.
	const String class_handle = collect_call_argument_diagnostic(original_instance, "reject_class_handle_argument");
	CHECK(class_handle.contains(R"(Argument 1 has type "RefCounted")"));
	CHECK_FALSE(class_handle.contains("FSNativeClass"));

	// Nothing above may depend on the front end: the restored script answers from its compiled
	// parameter descriptors alone and has to produce the identical text.
	for (const char *method : { "reject_width", "reject_converted_width", "reject_specialization",
				 "reject_handle", "reject_rest", "reject_bound", "reject_unbound",
				 "reject_captured_lambda", "reject_class_handle_argument" }) {
		CHECK(collect_call_argument_diagnostic(restored_instance, method) ==
				collect_call_argument_diagnostic(original_instance, method));
	}
}

TEST_CASE("[Modules][FoundryScript][CallArgumentDiagnostics] A rejected call reports unchanged error fields") {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> original;
	const Ref<FoundryScript> restored = load_call_argument_diagnostic_bytecode(
			call_argument_diagnostic_source, &resolver, &original);

	Variant original_owner;
	Object *original_instance = instantiate_call_argument_diagnostic_script(original, original_owner);
	Variant restored_owner;
	Object *restored_instance = instantiate_call_argument_diagnostic_script(restored, restored_owner);

	// The improved wording is formatting only. A C++ or extension caller reads the same three fields
	// it always did, with the parameter's carrier in `expected`.
	for (Object *instance : { original_instance, restored_instance }) {
		ScriptInstance *script_instance = instance->get_script_instance();
		REQUIRE(script_instance != nullptr);

		const Variant overflowing = int64_t(9223372036854775807LL);
		const Variant *width_arguments[1] = { &overflowing };
		Callable::CallError width_error;
		ERR_PRINT_OFF;
		script_instance->callp(SNAME("take_int"), width_arguments, 1, width_error);
		ERR_PRINT_ON;
		CHECK(width_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
		CHECK(width_error.argument == 0);
		CHECK(width_error.expected == int(Variant::INT));

		const Variant first = int64_t(1);
		const Variant second = "two";
		const Variant *rest_arguments[2] = { &first, &second };
		Callable::CallError rest_error;
		ERR_PRINT_OFF;
		script_instance->callp(SNAME("take_rest"), rest_arguments, 2, rest_error);
		ERR_PRINT_ON;
		CHECK(rest_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
		CHECK(rest_error.argument == 1);
		CHECK(rest_error.expected == int(Variant::INT));
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED
