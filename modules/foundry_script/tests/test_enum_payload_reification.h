/**************************************************************************/
/*  test_enum_payload_reification.h                                       */
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

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)

// Shared in-process compile helper and the disassembly helpers used to read an emitted store's type
// operand. Every module test header is compiled into the same generated test translation unit, so
// these includes do not duplicate test registrations.
#include "test_tuple_lowering.h"

#include "../foundry_script.h"

#include "core/error/error_macros.h"

#include "tests/test_macros.h"

namespace FSTests {

// The three enum-case construction spellings whose payload lowering the reified check distinguishes:
// a `Type[T]` handle, the concrete class, and a payload field typed by a method parameter the handle
// does not reify.
static Ref<FoundryScript> compile_enum_payload_reification_source() {
	return compile_bytecode_test_source(
			"class Receiver:\n"
			"\tenum Message:\n"
			"\t\tDetach\n"
			"\t\tAttach(index: int, owner: Self)\n"
			"\tenum Box[E]:\n"
			"\t\tEmpty\n"
			"\t\tFull(value: E)\n"
			"\n"
			"class Sub:\n"
			"\textends Receiver\n"
			"\n"
			"func via_handle[T: Receiver](handle: Type[T], value: Variant) -> Variant:\n"
			"\treturn handle.Message.Attach(1, value)\n"
			"\n"
			"func via_concrete(value: Variant) -> Variant:\n"
			"\treturn Sub.Message.Attach(1, value)\n"
			"\n"
			"func via_unrelated_parameter[T: Receiver, U](handle: Type[T], value: U) -> Variant:\n"
			"\treturn handle.Box[U].Full(value)\n"
			"\n"
			"static func via_static_handle[T: Receiver](handle: Type[T], value: Variant) -> Variant:\n"
			"\treturn handle.Message.Attach(1, value)\n");
}

static FSFunction *enum_payload_reification_function(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, FSFunction *>::ConstIterator element = p_script->get_member_functions().find(p_name);
	return element ? element->value : nullptr;
}

// Captures every engine error raised while a call runs, so a refusal can be asserted on by the reason
// it reported rather than only by the value it handed back.
struct EnumPayloadReificationErrorRecorder {
	EnumPayloadReificationErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~EnumPayloadReificationErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line,
			const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		EnumPayloadReificationErrorRecorder *self = static_cast<EnumPayloadReificationErrorRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	ErrorHandlerList handler;
	String messages;
};

// Runs the static handle spelling the way a C++ caller does: no instance, no receiver context. The
// reified check reads its type from the handle argument, so that is all the frame needs.
static Variant call_via_static_handle(FSFunction *p_function, const Variant &p_handle, const Variant &p_value,
		Callable::CallError &r_error) {
	const Variant *arguments[2] = { &p_handle, &p_value };
	return p_function->call(nullptr, arguments, 2, r_error, nullptr, nullptr, nullptr);
}

// A specialized handle whose single type argument has been freed. A never-registered script released
// immediately stands in for an argument freed while the handle is still reachable: `FSCache` holds a
// strong reference to every script a program can name, so this state has no source-level form and only
// a C++ caller can build it.
static Ref<FSSpecializedClassHandle> stale_specialized_handle(const Ref<FoundryScript> &p_script) {
	Ref<FSSpecializedClassHandle> handle;
	{
		Ref<FoundryScript> argument_script;
		argument_script.instantiate();
		REQUIRE(argument_script.is_valid());

		ContainerType argument;
		argument.builtin_type = Variant::OBJECT;
		argument.class_name = argument_script->get_instance_base_type();
		argument.script = argument_script;
		Vector<ContainerType> type_arguments;
		type_arguments.push_back(argument);

		handle = FSSpecializedClassHandle::create(p_script, type_arguments);
		REQUIRE(handle.is_valid());
		REQUIRE(handle->is_fully_live());
	}
	REQUIRE_FALSE(handle->is_fully_live());
	return handle;
}

TEST_CASE("[Modules][FoundryScript][EnumPayload] A reified check refuses a handle with a freed type argument") {
	const Ref<FoundryScript> script = compile_enum_payload_reification_source();
	FSFunction *via_static_handle = enum_payload_reification_function(script, SNAME("via_static_handle"));
	REQUIRE(via_static_handle != nullptr);

	// An instance of the handle's own class: exactly the value the check would admit once the freed
	// argument were materialized as the native class it captured, which is the substitution the refusal
	// exists to prevent.
	Callable::CallError instantiate_error;
	const Variant value = script->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);

	const Variant handle = stale_specialized_handle(script);

	EnumPayloadReificationErrorRecorder recorder;
	Callable::CallError call_error;
	ERR_PRINT_OFF;
	const Variant result = call_via_static_handle(via_static_handle, handle, value, call_error);
	ERR_PRINT_ON;

	// The refusal names the stale slot rather than the class name the freed script captured.
	CHECK(recorder.messages.contains("references a script that has been freed"));
	CHECK(recorder.messages.contains("type argument 0"));
	// The construction never completes, so no payload carrying an unchecked value is produced.
	CHECK(result.get_type() == Variant::NIL);
}

TEST_CASE("[Modules][FoundryScript][EnumPayload] A live specialized handle still checks the payload store") {
	const Ref<FoundryScript> script = compile_enum_payload_reification_source();
	FSFunction *via_static_handle = enum_payload_reification_function(script, SNAME("via_static_handle"));
	REQUIRE(via_static_handle != nullptr);

	// The script argues for itself: `FSCache` holds it, so the handle's argument stays live for the
	// whole case.
	ContainerType argument;
	argument.builtin_type = Variant::OBJECT;
	argument.class_name = script->get_instance_base_type();
	argument.script = script;
	Vector<ContainerType> type_arguments;
	type_arguments.push_back(argument);
	const Ref<FSSpecializedClassHandle> live_handle = FSSpecializedClassHandle::create(script, type_arguments);
	REQUIRE(live_handle.is_valid());
	REQUIRE(live_handle->is_fully_live());

	Callable::CallError instantiate_error;
	const Variant matching_value = script->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);

	{
		EnumPayloadReificationErrorRecorder recorder;
		Callable::CallError call_error;
		const Variant result = call_via_static_handle(via_static_handle, live_handle, matching_value, call_error);

		CHECK(recorder.messages.is_empty());
		// `Attach(index, owner)` builds `[tag, index, owner]`.
		REQUIRE(result.get_type() == Variant::ARRAY);
		const Array payload = result;
		CHECK(payload.size() == 3);
	}

	{
		// A value of some other class is still refused, and by the ordinary mismatch rather than by the
		// staleness guard.
		Ref<RefCounted> foreign_object;
		foreign_object.instantiate();
		const Variant foreign = foreign_object;

		EnumPayloadReificationErrorRecorder recorder;
		Callable::CallError call_error;
		ERR_PRINT_OFF;
		const Variant result = call_via_static_handle(via_static_handle, live_handle, foreign, call_error);
		ERR_PRINT_ON;

		CHECK(recorder.messages.contains("Trying to assign value of type"));
		CHECK_FALSE(recorder.messages.contains("has been freed"));
		CHECK(result.get_type() == Variant::NIL);
	}
}

TEST_CASE("[Modules][FoundryScript][EnumPayload] A handle-spelled payload field checks against the handle register") {
	const Ref<FoundryScript> script = compile_enum_payload_reification_source();

	const Vector<String> handle_stores = filter_disassembly_lines(
			disassemble_test_function(script, SNAME("via_handle")), "assign typed script");
	REQUIRE(handle_stores.size() == 1);
	// The represented type is a method type parameter, which erases to Variant and so is nameable by
	// no constant: the check reads the class off the handle the frame was given.
	CHECK(handle_stores[0].contains("assign typed script (stack("));

	const Vector<String> concrete_stores = filter_disassembly_lines(
			disassemble_test_function(script, SNAME("via_concrete")), "assign typed script");
	REQUIRE(concrete_stores.size() == 1);
	// The concrete spelling names a class at compile time, so its operand still renders as that class.
	CHECK(concrete_stores[0].contains("assign typed script (Sub)"));
}

TEST_CASE("[Modules][FoundryScript][EnumPayload] A payload field typed by an unreified parameter stays erased") {
	const Ref<FoundryScript> script = compile_enum_payload_reification_source();

	// `U` is not what the receiver handle reifies, so the frame holds no evidence about it and the
	// argument keeps the erasure every other method-generic slot has.
	CHECK(filter_disassembly_lines(
			disassemble_test_function(script, SNAME("via_unrelated_parameter")), "assign typed script")
					.is_empty());
}

} // namespace FSTests

#endif // TOOLS_ENABLED && DEBUG_ENABLED
