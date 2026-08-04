/**************************************************************************/
/*  test_callable.h                                                       */
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

#include "core/object/class_db.h"
#include "core/object/message_queue.h"
#include "core/object/object.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestCallable {

class TestClass : public Object {
	FOUNDRY_CLASS(TestClass, Object);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("test_func_1", "foo", "bar"), &TestClass::test_func_1);
		ClassDB::bind_method(D_METHOD("test_func_2", "foo", "bar", "baz"), &TestClass::test_func_2);
		ClassDB::bind_static_method("TestClass", D_METHOD("test_func_5", "foo", "bar"), &TestClass::test_func_5);
		ClassDB::bind_static_method("TestClass", D_METHOD("test_func_6", "foo", "bar", "baz"), &TestClass::test_func_6);

		{
			MethodInfo mi;
			mi.name = "test_func_7";
			mi.arguments.push_back(PropertyInfo(Variant::INT, "foo"));
			mi.arguments.push_back(PropertyInfo(Variant::INT, "bar"));

			ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "test_func_7", &TestClass::test_func_7, mi, varray(), false);
		}

		{
			MethodInfo mi;
			mi.name = "test_func_8";
			mi.arguments.push_back(PropertyInfo(Variant::INT, "foo"));
			mi.arguments.push_back(PropertyInfo(Variant::INT, "bar"));
			mi.arguments.push_back(PropertyInfo(Variant::INT, "baz"));

			ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "test_func_8", &TestClass::test_func_8, mi, varray(), false);
		}
	}

public:
	void test_func_1(int p_foo, int p_bar) {}
	void test_func_2(int p_foo, int p_bar, int p_baz) {}

	int test_func_3(int p_foo, int p_bar) const { return 0; }
	int test_func_4(int p_foo, int p_bar, int p_baz) const { return 0; }

	static void test_func_5(int p_foo, int p_bar) {}
	static void test_func_6(int p_foo, int p_bar, int p_baz) {}

	void test_func_7(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {}
	void test_func_8(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {}
};

TEST_CASE("[Callable] Argument count") {
	TestClass *my_test = memnew(TestClass);

	// Bound methods tests.

	// Test simple methods.
	Callable callable_1 = Callable(my_test, "test_func_1");
	CHECK_EQ(callable_1.get_argument_count(), 2);
	Callable callable_2 = Callable(my_test, "test_func_2");
	CHECK_EQ(callable_2.get_argument_count(), 3);
	Callable callable_3 = Callable(my_test, "test_func_5");
	CHECK_EQ(callable_3.get_argument_count(), 2);
	Callable callable_4 = Callable(my_test, "test_func_6");
	CHECK_EQ(callable_4.get_argument_count(), 3);

	// Test vararg methods.
	Callable callable_vararg_1 = Callable(my_test, "test_func_7");
	CHECK_MESSAGE(callable_vararg_1.get_argument_count() == 2, "vararg Callable should return the number of declared arguments");
	Callable callable_vararg_2 = Callable(my_test, "test_func_8");
	CHECK_MESSAGE(callable_vararg_2.get_argument_count() == 3, "vararg Callable should return the number of declared arguments");

	// Callable MP tests.

	// Test simple methods.
	Callable callable_mp_1 = callable_mp(my_test, &TestClass::test_func_1);
	CHECK_EQ(callable_mp_1.get_argument_count(), 2);
	Callable callable_mp_2 = callable_mp(my_test, &TestClass::test_func_2);
	CHECK_EQ(callable_mp_2.get_argument_count(), 3);
	Callable callable_mp_3 = callable_mp(my_test, &TestClass::test_func_3);
	CHECK_EQ(callable_mp_3.get_argument_count(), 2);
	Callable callable_mp_4 = callable_mp(my_test, &TestClass::test_func_4);
	CHECK_EQ(callable_mp_4.get_argument_count(), 3);

	// Test static methods.
	Callable callable_mp_static_1 = callable_mp_static(&TestClass::test_func_5);
	CHECK_EQ(callable_mp_static_1.get_argument_count(), 2);
	Callable callable_mp_static_2 = callable_mp_static(&TestClass::test_func_6);
	CHECK_EQ(callable_mp_static_2.get_argument_count(), 3);

	// Test bind.
	Callable callable_mp_bind_1 = callable_mp_2.bind(1);
	CHECK_MESSAGE(callable_mp_bind_1.get_argument_count() == 2, "bind should subtract from the argument count");
	Callable callable_mp_bind_2 = callable_mp_2.bind(1, 2);
	CHECK_MESSAGE(callable_mp_bind_2.get_argument_count() == 1, "bind should subtract from the argument count");

	// Test unbind.
	Callable callable_mp_unbind_1 = callable_mp_2.unbind(1);
	CHECK_MESSAGE(callable_mp_unbind_1.get_argument_count() == 4, "unbind should add to the argument count");
	Callable callable_mp_unbind_2 = callable_mp_2.unbind(2);
	CHECK_MESSAGE(callable_mp_unbind_2.get_argument_count() == 5, "unbind should add to the argument count");

	memdelete(my_test);
}

class TestBoundUnboundArgumentCount : public Object {
	FOUNDRY_CLASS(TestBoundUnboundArgumentCount, Object);

protected:
	static void _bind_methods() {
		ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "test_func", &TestBoundUnboundArgumentCount::test_func, MethodInfo("test_func"));
	}

public:
	Variant test_func(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
		Array result;
		result.resize(p_argcount);
		for (int i = 0; i < p_argcount; i++) {
			result[i] = *p_args[i];
		}
		return result;
	}

	static String get_output(const Callable &p_callable) {
		Array effective_args = { 7, 8, 9 };
		effective_args.resize(3 - p_callable.get_unbound_arguments_count());
		effective_args.append_array(p_callable.get_bound_arguments());

		return vformat(
				"%d %d %s %s %s",
				p_callable.get_unbound_arguments_count(),
				p_callable.get_bound_arguments_count(),
				p_callable.get_bound_arguments(),
				p_callable.call(7, 8, 9),
				effective_args);
	}
};

TEST_CASE("[Callable] Bound and unbound argument count") {
	String (*get_output)(const Callable &) = TestBoundUnboundArgumentCount::get_output;

	TestBoundUnboundArgumentCount *test_instance = memnew(TestBoundUnboundArgumentCount);

	Callable test_func = Callable(test_instance, "test_func");

	CHECK(get_output(test_func) == "0 0 [] [7, 8, 9] [7, 8, 9]");
	CHECK(get_output(test_func.bind(1, 2)) == "0 2 [1, 2] [7, 8, 9, 1, 2] [7, 8, 9, 1, 2]");
	CHECK(get_output(test_func.bind(1, 2).unbind(1)) == "1 2 [1, 2] [7, 8, 1, 2] [7, 8, 1, 2]");
	CHECK(get_output(test_func.bind(1, 2).unbind(1).bind(3, 4)) == "0 3 [3, 1, 2] [7, 8, 9, 3, 1, 2] [7, 8, 9, 3, 1, 2]");
	CHECK(get_output(test_func.bind(1, 2).unbind(1).bind(3, 4).unbind(1)) == "1 3 [3, 1, 2] [7, 8, 3, 1, 2] [7, 8, 3, 1, 2]");

	CHECK(get_output(test_func.bind(1).bind(2).bind(3).unbind(1)) == "1 3 [3, 2, 1] [7, 8, 3, 2, 1] [7, 8, 3, 2, 1]");
	CHECK(get_output(test_func.bind(1).bind(2).unbind(1).bind(3)) == "0 2 [2, 1] [7, 8, 9, 2, 1] [7, 8, 9, 2, 1]");
	CHECK(get_output(test_func.bind(1).unbind(1).bind(2).bind(3)) == "0 2 [3, 1] [7, 8, 9, 3, 1] [7, 8, 9, 3, 1]");
	CHECK(get_output(test_func.unbind(1).bind(1).bind(2).bind(3)) == "0 2 [3, 2] [7, 8, 9, 3, 2] [7, 8, 9, 3, 2]");

	CHECK(get_output(test_func.unbind(1).unbind(1).unbind(1).bind(1, 2, 3)) == "0 0 [] [7, 8, 9] [7, 8, 9]");
	CHECK(get_output(test_func.unbind(1).unbind(1).bind(1, 2, 3).unbind(1)) == "1 1 [1] [7, 8, 1] [7, 8, 1]");
	CHECK(get_output(test_func.unbind(1).bind(1, 2, 3).unbind(1).unbind(1)) == "2 2 [1, 2] [7, 1, 2] [7, 1, 2]");
	CHECK(get_output(test_func.bind(1, 2, 3).unbind(1).unbind(1).unbind(1)) == "3 3 [1, 2, 3] [1, 2, 3] [1, 2, 3]");

	memdelete(test_instance);
}

TEST_CASE("[Callable] Is async") {
	TestClass *my_test = memnew(TestClass);

	// Native methods are synchronous, so they are never async.
	Callable native_callable = Callable(my_test, "test_func_1");
	CHECK_FALSE(native_callable.is_async());

	// Binding/unbinding a synchronous target keeps it synchronous.
	CHECK_FALSE(native_callable.bind(1).is_async());
	CHECK_FALSE(native_callable.unbind(1).is_async());
	CHECK_FALSE(native_callable.bind(1, 2).unbind(1).is_async());

	// Invalid and null callables are not async.
	CHECK_FALSE(Callable(my_test, "nonexistent_method").is_async());
	CHECK_FALSE(Callable().is_async());

	memdelete(my_test);
}

class TestUnsignedArgumentReceiver : public Object {
	FOUNDRY_CLASS(TestUnsignedArgumentReceiver, Object);

public:
	int call_count = 0;
	int received_tile_id = -1;
	uint64_t received_generation = 0;
	uint32_t received_mask = 0;
	uint32_t received_flags = 0;

	static int static_call_count;
	static uint32_t static_received_value;

	void receive(int p_tile_id, uint64_t p_generation) {
		call_count++;
		received_tile_id = p_tile_id;
		received_generation = p_generation;
	}

	void receive_mask(uint32_t p_mask) {
		call_count++;
		received_mask = p_mask;
	}

	// Mirrors `Object::connect(..., uint32_t)`: a non-void native target that takes an unsigned
	// flags argument and returns a sentinel `Error`.
	Error receive_flags(uint32_t p_flags) {
		call_count++;
		received_flags = p_flags;
		return ERR_BUSY;
	}

	static void receive_static(uint32_t p_value) {
		static_call_count++;
		static_received_value = p_value;
	}

	// `Variant::operator Color()` only reads `Variant::INT`, so an unsigned argument must not be
	// silently accepted here.
	void receive_color(Color p_color) {
		call_count++;
	}
};

int TestUnsignedArgumentReceiver::static_call_count = 0;
uint32_t TestUnsignedArgumentReceiver::static_received_value = 0;

// Tagged `[SceneTree]` because the deferred subcase needs a live `MessageQueue`.
TEST_CASE("[SceneTree][Callable] Unsigned native parameter dispatch") {
	TestUnsignedArgumentReceiver *receiver = memnew(TestUnsignedArgumentReceiver);
	TestUnsignedArgumentReceiver::static_call_count = 0;
	TestUnsignedArgumentReceiver::static_received_value = 0;

	// Above `UINT32_MAX` so a carrier that truncates the value stays visible in the assertions.
	const uint64_t generation = 4294967297ULL;
	const uint32_t mask = 0xA5A5A5A5u;
	const uint32_t flags = 0xC0FFEEu;
	const uint32_t static_value = 0xDEADBEEFu;

	SUBCASE("bound argument") {
		ErrorDetector detector;

		Callable callable = callable_mp(receiver, &TestUnsignedArgumentReceiver::receive).bind(7, generation);
		Callable::CallError error;
		Variant result;
		callable.callp(nullptr, 0, result, error);

		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK_FALSE(detector.has_error);
		CHECK(receiver->call_count == 1);
		CHECK(receiver->received_tile_id == 7);
		CHECK(receiver->received_generation == generation);
	}

	SUBCASE("deferred call") {
		ErrorDetector detector;

		callable_mp(receiver, &TestUnsignedArgumentReceiver::receive).call_deferred(7, generation);
		MessageQueue::get_singleton()->flush();

		CHECK_FALSE(detector.has_error);
		CHECK(receiver->call_count == 1);
		CHECK(receiver->received_tile_id == 7);
		CHECK(receiver->received_generation == generation);
	}

	SUBCASE("signal emission with a bound argument") {
		Object *emitter = memnew(Object);
		emitter->add_user_signal(MethodInfo("tile_activation_queued"));
		emitter->connect("tile_activation_queued",
				callable_mp(receiver, &TestUnsignedArgumentReceiver::receive).bind(7, generation),
				Object::CONNECT_ONE_SHOT);

		ErrorDetector detector;
		emitter->emit_signal("tile_activation_queued");

		CHECK_FALSE(detector.has_error);
		CHECK(receiver->call_count == 1);
		CHECK(receiver->received_tile_id == 7);
		CHECK(receiver->received_generation == generation);

		memdelete(emitter);
	}

	SUBCASE("signal-emitted unsigned argument") {
		// Matches `EditorPropertyLayersGrid` emitting a `uint32_t` mask onto a `uint32_t` receiver.
		Object *emitter = memnew(Object);
		emitter->add_user_signal(MethodInfo("flag_changed", PropertyInfo(Variant::INT, "value")));
		emitter->connect("flag_changed", callable_mp(receiver, &TestUnsignedArgumentReceiver::receive_mask), Object::CONNECT_ONE_SHOT);

		ErrorDetector detector;
		emitter->emit_signal("flag_changed", mask);

		CHECK_FALSE(detector.has_error);
		CHECK(receiver->call_count == 1);
		CHECK(receiver->received_mask == mask);

		memdelete(emitter);
	}

	SUBCASE("non-void member target with uint32_t") {
		// Matches `Object::connect(..., uint32_t)` reached through `callable_mp(...).call_deferred`.
		ErrorDetector detector;

		Callable callable = callable_mp(receiver, &TestUnsignedArgumentReceiver::receive_flags).bind(flags);
		Callable::CallError error;
		Variant result;
		callable.callp(nullptr, 0, result, error);

		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK_FALSE(detector.has_error);
		CHECK(receiver->call_count == 1);
		CHECK(receiver->received_flags == flags);
		CHECK((Error)result == ERR_BUSY);
	}

	SUBCASE("static callable with bound uint32_t") {
		// Matches OpenXR render-thread `callable_mp_static(...).bind(uint32_t, ...)` sites.
		ErrorDetector detector;

		Callable callable = callable_mp_static(&TestUnsignedArgumentReceiver::receive_static).bind(static_value);
		Callable::CallError error;
		Variant result;
		callable.callp(nullptr, 0, result, error);

		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK_FALSE(detector.has_error);
		CHECK(TestUnsignedArgumentReceiver::static_call_count == 1);
		CHECK(TestUnsignedArgumentReceiver::static_received_value == static_value);
	}

	SUBCASE("value beyond the signed range") {
		ErrorDetector detector;

		// The whole point of the unsigned carrier is that this value survives; it has no signed
		// 64-bit representation, so it would be corrupted if the argument were routed through `INT`.
		const uint64_t unrepresentable_as_signed = UINT64_MAX;
		Callable callable = callable_mp(receiver, &TestUnsignedArgumentReceiver::receive).bind(7, unrepresentable_as_signed);
		Callable::CallError error;
		Variant result;
		callable.callp(nullptr, 0, result, error);

		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK_FALSE(detector.has_error);
		CHECK(receiver->call_count == 1);
		CHECK(receiver->received_tile_id == 7);
		CHECK(receiver->received_generation == unrepresentable_as_signed);
	}

	SUBCASE("parameter whose conversion cannot read the unsigned carrier") {
		ErrorDetector detector;

		Callable callable = callable_mp(receiver, &TestUnsignedArgumentReceiver::receive_color).bind(generation);
		Callable::CallError error;
		Variant result;
		callable.callp(nullptr, 0, result, error);

		CHECK(error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
		CHECK(error.expected == Variant::COLOR);
	}

	memdelete(receiver);
}

} // namespace TestCallable
