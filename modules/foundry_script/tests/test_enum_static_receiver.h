/**************************************************************************/
/*  test_enum_static_receiver.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_compiler.h"
#include "../fs_function.h"
#include "../fs_parser.h"

#include "core/error/error_macros.h"
#include "core/object/callable_method_pointer.h"

#include "tests/test_macros.h"

// Coverage for issue #1581: `OPCODE_CALL_ENUM` invokes a static enum function with no
// `FSStaticSelfContext`, unlike every other static dispatch boundary #1544 supplies one for. An enum
// has no ancestors and no subtypes, so `Self` inside an enum function's signature is resolved eagerly
// to the exact enum type at analysis time (`FSAnalyzer::enum_self_type`) instead of being left for
// late runtime binding; the compiler's `FSParser::DataType::ENUM` case never sets `is_self_type`. This
// suite proves that invariant for real compiled signatures, exercises the detector in isolation, and
// proves the runtime guard added to `OPCODE_CALL_ENUM` actually fires if the invariant is ever broken.

namespace FSTests {

// Exposes the private compiled signature of an `FSFunction` so a test can both read it (to prove the
// invariant holds for real compiled code) and deliberately corrupt it (to prove the runtime guard that
// depends on the invariant actually catches a violation).
class TestEnumStaticReceiverAccessor {
public:
	static void mark_return_type_self_referencing(FSFunction *p_function) {
		p_function->return_type.is_self_type = true;
	}
};

static Vector<FSStaticSelfContext> &enum_static_receiver_probe_records() {
	static thread_local Vector<FSStaticSelfContext> records;
	return records;
}

// Invoked from inside a script frame reached through enum dispatch. Records the descriptor that
// frame received, or an invalid descriptor when the frame received none.
static void enum_static_receiver_probe() {
	const FSStaticSelfContext *context = FSFunction::get_current_static_self_context();
	enum_static_receiver_probe_records().push_back(context != nullptr ? *context : FSStaticSelfContext());
}

static Variant enum_static_receiver_probe_callable() {
	return Variant(callable_mp_static(&enum_static_receiver_probe));
}

struct EnumStaticReceiverLanguageScope {
	EnumStaticReceiverLanguageScope() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
		enum_static_receiver_probe_records().clear();
	}
	~EnumStaticReceiverLanguageScope() {
		enum_static_receiver_probe_records().clear();
	}
};

// Captures every engine error raised while a call runs, so a rejection can be asserted on by its
// reported reason instead of only by whether the call produced a value.
struct EnumStaticReceiverErrorRecorder {
	EnumStaticReceiverErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~EnumStaticReceiverErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line,
			const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		EnumStaticReceiverErrorRecorder *self = static_cast<EnumStaticReceiverErrorRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	ErrorHandlerList handler;
	String messages;
};

static Ref<FoundryScript> compile_enum_static_receiver_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_enum_static_receiver_%d.fs", unique_index++);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	FSParser parser;
	REQUIRE(parser.parse(p_source, script->get_path(), false) == OK);

	FSAnalyzer analyzer(&parser);
	String reported_errors;
	const Error analyze_result = analyzer.analyze();
	for (const FSParser::ParserError &parse_error : parser.get_errors()) {
		reported_errors += "\n" + parse_error.message;
	}
	REQUIRE_MESSAGE(analyze_result == OK, reported_errors);

	FSCompiler compiler;
	REQUIRE(compiler.compile(&parser, script.ptr(), false) == OK);
	REQUIRE(script->reload() == OK);

	return script;
}

static Ref<FoundryScript> enum_static_receiver_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

// Calls `p_method` through `p_receiver` the way any engine caller would: virtually, through
// `Object::callp`.
static Variant enum_static_receiver_call(const Ref<FoundryScript> &p_receiver, const StringName &p_method,
		const Vector<Variant> &p_arguments, Callable::CallError &r_error) {
	LocalVector<const Variant *> argument_pointers;
	argument_pointers.resize(p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		argument_pointers[i] = &p_arguments[i];
	}
	Object *receiver = p_receiver.ptr();
	return receiver->callp(p_method, argument_pointers.ptr(), p_arguments.size(), r_error);
}

static const char *ENUM_STATIC_RECEIVER_SOURCE = R"(
class Runner:
	enum Status:
		READY = 1
		DONE = 2

		static func make(cb: Callable) -> Self:
			cb.call()
			return READY

		static func echo(cb: Callable, value: Self) -> Self:
			cb.call()
			return value

		static func wrap(cb: Callable) -> Array[Self]:
			cb.call()
			return [READY]

		func identity() -> Self:
			return self

	static func run_make(cb: Callable) -> void:
		Status.make(cb)

	static func run_echo(cb: Callable) -> void:
		Status.echo(cb, Status.DONE)

	static func run_wrap(cb: Callable) -> void:
		Status.wrap(cb)
)";

TEST_CASE("[Modules][FoundryScript][EnumStaticReceiver] Self in an enum function signature never carries the static-receiver marker") {
	EnumStaticReceiverLanguageScope language;

	const Ref<FoundryScript> script = compile_enum_static_receiver_source(ENUM_STATIC_RECEIVER_SOURCE);
	const Ref<FoundryScript> runner = enum_static_receiver_subclass(script, SNAME("Runner"));
	REQUIRE(runner.is_valid());

	FSFunction *make = runner->get_enum_function(SNAME("Status"), SNAME("make"), true);
	FSFunction *echo = runner->get_enum_function(SNAME("Status"), SNAME("echo"), true);
	FSFunction *wrap = runner->get_enum_function(SNAME("Status"), SNAME("wrap"), true);
	FSFunction *identity = runner->get_enum_function(SNAME("Status"), SNAME("identity"), false);
	REQUIRE(make != nullptr);
	REQUIRE(echo != nullptr);
	REQUIRE(wrap != nullptr);
	REQUIRE(identity != nullptr);

	// A bare `Self` return type.
	CHECK_FALSE(make->has_self_referencing_signature());
	// `Self` in both a parameter and the return type.
	CHECK_FALSE(echo->has_self_referencing_signature());
	// `Self` nested inside a generic container's element type.
	CHECK_FALSE(wrap->has_self_referencing_signature());
	// A non-static (witness) enum function using `Self` as its return type.
	CHECK_FALSE(identity->has_self_referencing_signature());
}

TEST_CASE("[Modules][FoundryScript][EnumStaticReceiver] Calling a static enum function through the VM supplies no static receiver") {
	EnumStaticReceiverLanguageScope language;

	const Ref<FoundryScript> script = compile_enum_static_receiver_source(ENUM_STATIC_RECEIVER_SOURCE);
	const Ref<FoundryScript> runner = enum_static_receiver_subclass(script, SNAME("Runner"));
	REQUIRE(runner.is_valid());

	Callable::CallError error;
	enum_static_receiver_call(runner, SNAME("run_make"), { enum_static_receiver_probe_callable() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	REQUIRE_EQ(enum_static_receiver_probe_records().size(), 1);
	// Enum dispatch never supplies a descriptor; the frame the enum function runs in must see none.
	CHECK(enum_static_receiver_probe_records()[0] == FSStaticSelfContext());
	CHECK_FALSE(enum_static_receiver_probe_records()[0].is_valid());
}

TEST_CASE("[Modules][FoundryScript][EnumStaticReceiver] references_self_type detects Self nested in generics and containers") {
	FSDataType plain_int;
	plain_int.kind = FSDataType::BUILTIN;
	plain_int.builtin_type = Variant::INT;
	CHECK_FALSE(plain_int.references_self_type());

	FSDataType bare_self;
	bare_self.kind = FSDataType::FOUNDRY_SCRIPT;
	bare_self.is_self_type = true;
	CHECK(bare_self.references_self_type());

	// `Self` nested as a specialized handle's type argument, e.g. `Crate[Self]`.
	FSDataType crate_of_self;
	crate_of_self.kind = FSDataType::FOUNDRY_SCRIPT;
	crate_of_self.type_arguments.push_back(bare_self);
	CHECK(crate_of_self.references_self_type());

	// `Self` nested as a typed container's element type, e.g. `Array[Self]`.
	FSDataType array_of_self;
	array_of_self.kind = FSDataType::BUILTIN;
	array_of_self.builtin_type = Variant::ARRAY;
	array_of_self.container_element_types.push_back(bare_self);
	CHECK(array_of_self.references_self_type());

	// A container of a type that itself nests `Self` two levels deep, e.g. `Array[Crate[Self]]`.
	FSDataType array_of_crate_of_self;
	array_of_crate_of_self.kind = FSDataType::BUILTIN;
	array_of_crate_of_self.builtin_type = Variant::ARRAY;
	array_of_crate_of_self.container_element_types.push_back(crate_of_self);
	CHECK(array_of_crate_of_self.references_self_type());
}

TEST_CASE("[Modules][FoundryScript][EnumStaticReceiver] Enum dispatch refuses a function whose signature would need a receiver it cannot supply") {
	EnumStaticReceiverLanguageScope language;

	const Ref<FoundryScript> script = compile_enum_static_receiver_source(ENUM_STATIC_RECEIVER_SOURCE);
	const Ref<FoundryScript> runner = enum_static_receiver_subclass(script, SNAME("Runner"));
	REQUIRE(runner.is_valid());

	FSFunction *make = runner->get_enum_function(SNAME("Status"), SNAME("make"), true);
	REQUIRE(make != nullptr);
	REQUIRE_FALSE(make->has_self_referencing_signature());

	// A real compiler can never produce this signature for an enum function (see the first test
	// case above); this corrupts the compiled function in place to simulate the exact regression
	// this issue exists to guard against, and proves the VM opcode refuses to run it rather than
	// calling with a missing receiver.
	TestEnumStaticReceiverAccessor::mark_return_type_self_referencing(make);
	REQUIRE(make->has_self_referencing_signature());

	EnumStaticReceiverErrorRecorder recorder;
	Callable::CallError error;
	enum_static_receiver_call(runner, SNAME("run_make"), { enum_static_receiver_probe_callable() }, error);

	CHECK(recorder.messages.contains(
			R"(its signature references "Self", but enum dispatch has no static receiver to resolve it against.)"));
	// The guard must refuse the call before the enum function's body runs.
	CHECK(enum_static_receiver_probe_records().is_empty());
}

} // namespace FSTests
