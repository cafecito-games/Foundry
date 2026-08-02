/**************************************************************************/
/*  test_static_self_context.h                                            */
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

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_compiler.h"
#include "../fs_conformance_registry.h"
#include "../fs_function.h"
#include "../fs_parser.h"

#include "core/object/callable_method_pointer.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

// Coverage for the immutable static receiver descriptor a static call delivers to the frame it
// starts. Each script under test takes a `Callable` and invokes it from inside the callee body, so
// the probe runs on the callee's frame and reads exactly what that frame was given.

namespace FSTests {

static Vector<FSStaticSelfContext> &static_self_probe_records() {
	static thread_local Vector<FSStaticSelfContext> records;
	return records;
}

// Invoked from inside a script frame. Records the descriptor that frame received, or an invalid
// descriptor when the frame received none.
static void static_self_probe() {
	const FSStaticSelfContext *context = FSFunction::get_current_static_self_context();
	static_self_probe_records().push_back(context != nullptr ? *context : FSStaticSelfContext());
}

struct StaticSelfLanguageScope {
	StaticSelfLanguageScope() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
		static_self_probe_records().clear();
	}
	~StaticSelfLanguageScope() {
		static_self_probe_records().clear();
	}
};

// Removes the retroactive conformances a test source registered so later tests cannot dispatch
// through a witness that belongs to a script this one compiled.
struct StaticSelfConformanceScope {
	String path;

	explicit StaticSelfConformanceScope(const String &p_path) :
			path(p_path) {}
	~StaticSelfConformanceScope() {
		FSConformanceRegistry::get_singleton()->clear_file(path);
		FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(path);
	}
};

static Ref<FoundryScript> compile_static_self_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_static_self_context_%d.fs", unique_index++);

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

static Ref<FoundryScript> static_self_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

// Calls `p_method` through `p_receiver` the way any engine caller would: virtually, through
// `Object::callp`, so the receiver's own dispatch entry point decides what the frame is told.
static Variant call_through_handle(const Ref<RefCounted> &p_receiver, const StringName &p_method,
		const Vector<Variant> &p_arguments, Callable::CallError &r_error) {
	LocalVector<const Variant *> argument_pointers;
	argument_pointers.resize(p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		argument_pointers[i] = &p_arguments[i];
	}
	Object *receiver = p_receiver.ptr();
	return receiver->callp(p_method, argument_pointers.ptr(), p_arguments.size(), r_error);
}

static Variant probe_callable_value() {
	return Variant(callable_mp_static(&static_self_probe));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] An inherited static method sees the derived receiver") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Base:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const Ref<FoundryScript> base = static_self_subclass(script, SNAME("Base"));
	const Ref<FoundryScript> derived = static_self_subclass(script, SNAME("Derived"));
	REQUIRE(base.is_valid());
	REQUIRE(derived.is_valid());

	Callable::CallError error;
	call_through_handle(derived, SNAME("probe"), { probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	REQUIRE_EQ(static_self_probe_records().size(), 1);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_script(derived));
	CHECK_FALSE(static_self_probe_records()[0] == FSStaticSelfContext::for_script(base));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] An explicit base handle keeps the base as the receiver") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Base:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const Ref<FoundryScript> base = static_self_subclass(script, SNAME("Base"));
	REQUIRE(base.is_valid());

	Callable::CallError error;
	call_through_handle(base, SNAME("probe"), { probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	REQUIRE_EQ(static_self_probe_records().size(), 1);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_script(base));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] Inherited delegation keeps the original receiver") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Base:\n"
			"\tstatic func outer(cb: Callable) -> void:\n"
			"\t\tinner(cb)\n"
			"\n"
			"\tstatic func inner(cb: Callable) -> void:\n"
			"\t\tcb.call()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const Ref<FoundryScript> derived = static_self_subclass(script, SNAME("Derived"));
	REQUIRE(derived.is_valid());

	Callable::CallError error;
	call_through_handle(derived, SNAME("outer"), { probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	// `inner()` is reached without naming a class, so it must not reset the receiver to the class
	// that declares the running function.
	REQUIRE_EQ(static_self_probe_records().size(), 1);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_script(derived));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] A super delegation keeps the original receiver") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Base:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tsuper.probe(cb)\n");

	const Ref<FoundryScript> derived = static_self_subclass(script, SNAME("Derived"));
	REQUIRE(derived.is_valid());

	Callable::CallError error;
	call_through_handle(derived, SNAME("probe"), { probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	REQUIRE_EQ(static_self_probe_records().size(), 1);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_script(derived));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] A nested call through another handle restores the outer receiver") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Other:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n"
			"\n"
			"class Base:\n"
			"\tstatic func outer(cb: Callable, other) -> void:\n"
			"\t\tother.probe(cb)\n"
			"\t\tcb.call()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const Ref<FoundryScript> other = static_self_subclass(script, SNAME("Other"));
	const Ref<FoundryScript> derived = static_self_subclass(script, SNAME("Derived"));
	REQUIRE(other.is_valid());
	REQUIRE(derived.is_valid());

	Callable::CallError error;
	call_through_handle(derived, SNAME("outer"), { probe_callable_value(), Variant(other) }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	REQUIRE_EQ(static_self_probe_records().size(), 2);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_script(other));
	CHECK(static_self_probe_records()[1] == FSStaticSelfContext::for_script(derived));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] A specialized handle delivers its concrete type arguments") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Crate[T]:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n");

	const Ref<FoundryScript> crate = static_self_subclass(script, SNAME("Crate"));
	REQUIRE(crate.is_valid());

	ContainerType image_texture;
	image_texture.builtin_type = Variant::OBJECT;
	image_texture.class_name = SNAME("ImageTexture");

	ContainerType resource;
	resource.builtin_type = Variant::OBJECT;
	resource.class_name = SNAME("Resource");

	Vector<ContainerType> image_arguments;
	image_arguments.push_back(image_texture);
	Vector<ContainerType> resource_arguments;
	resource_arguments.push_back(resource);

	Callable::CallError error;
	call_through_handle(FSSpecializedClassHandle::create(crate, image_arguments), SNAME("probe"),
			{ probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	call_through_handle(FSSpecializedClassHandle::create(crate, resource_arguments), SNAME("probe"),
			{ probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	REQUIRE_EQ(static_self_probe_records().size(), 2);
	const FSStaticSelfContext &image_context = static_self_probe_records()[0];
	const FSStaticSelfContext &resource_context = static_self_probe_records()[1];

	CHECK(image_context == FSStaticSelfContext::for_specialized_script(crate, image_arguments));
	CHECK(resource_context == FSStaticSelfContext::for_specialized_script(crate, resource_arguments));
	// `Crate[ImageTexture]` and `Crate[Resource]` share a script, so only the retained type
	// arguments tell them apart.
	CHECK_FALSE(image_context == resource_context);
	CHECK(image_context.get_type_name().contains("ImageTexture"));
	CHECK_NE(image_context.get_type_name(), resource_context.get_type_name());
	// An unspecialized receiver is distinct from every specialization of the same script.
	CHECK_FALSE(image_context == FSStaticSelfContext::for_script(crate));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] A native witness sees the exact native subclass") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"trait Probing:\n"
			"\tabstract static func probe(cb: Callable) -> void\n"
			"\n"
			"extend RefCounted uses Probing:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n");
	StaticSelfConformanceScope conformance_scope(script->get_script_path());

	REQUIRE(FSConformanceRegistry::get_singleton()->find_native_witness_function(
					SNAME("RefCounted"), SNAME("probe")) != nullptr);

	Ref<FSNativeClass> resource_handle = memnew(FSNativeClass(SNAME("Resource")));
	Callable::CallError error;
	call_through_handle(resource_handle, SNAME("probe"), { probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	// The witness is declared on `RefCounted`; the frame must still be told the handle the call was
	// made through.
	REQUIRE_EQ(static_self_probe_records().size(), 1);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_native_class(SNAME("Resource")));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] A builtin static witness sees its builtin type") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"trait Probing:\n"
			"\tabstract static func probe(cb: Callable) -> void\n"
			"\n"
			"extend Vector2 uses Probing:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n"
			"\n"
			"class Runner:\n"
			"\tstatic func run(cb: Callable) -> void:\n"
			"\t\tVector2.probe(cb)\n");
	StaticSelfConformanceScope conformance_scope(script->get_script_path());

	const Ref<FoundryScript> runner = static_self_subclass(script, SNAME("Runner"));
	REQUIRE(runner.is_valid());

	Callable::CallError error;
	call_through_handle(runner, SNAME("run"), { probe_callable_value() }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	REQUIRE_EQ(static_self_probe_records().size(), 1);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_builtin_type(Variant::VECTOR2));
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] A suspended call resumes with the same receiver") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Base:\n"
			"\tstatic func probe(cb: Callable, source: Object) -> void:\n"
			"\t\tawait source.script_changed\n"
			"\t\tcb.call()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const Ref<FoundryScript> derived = static_self_subclass(script, SNAME("Derived"));
	REQUIRE(derived.is_valid());

	Object *signal_source = memnew(Object);
	Callable::CallError error;
	const Variant suspended = call_through_handle(derived, SNAME("probe"),
			{ probe_callable_value(), Variant(signal_source) }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	Ref<FSFunctionState> function_state = suspended;
	REQUIRE(function_state.is_valid());
	CHECK(static_self_probe_records().is_empty());

	function_state->resume();

	REQUIRE_EQ(static_self_probe_records().size(), 1);
	CHECK(static_self_probe_records()[0] == FSStaticSelfContext::for_script(derived));

	memdelete(signal_source);
}

struct StaticSelfConcurrentCall {
	Ref<FoundryScript> receiver;
	int iteration_count = 0;
	SafeNumeric<int> mismatch_count;
	SafeNumeric<int> completed_count;
};

static void run_static_self_concurrent_calls(void *p_data) {
	StaticSelfConcurrentCall *task = static_cast<StaticSelfConcurrentCall *>(p_data);
	const FSStaticSelfContext expected = FSStaticSelfContext::for_script(task->receiver);
	for (int i = 0; i < task->iteration_count; i++) {
		static_self_probe_records().clear();
		Callable::CallError error;
		call_through_handle(task->receiver, SNAME("probe"), { probe_callable_value() }, error);
		if (error.error != Callable::CallError::CALL_OK) {
			task->mismatch_count.increment();
			continue;
		}
		if (static_self_probe_records().size() != 1 || static_self_probe_records()[0] != expected) {
			task->mismatch_count.increment();
			continue;
		}
		task->completed_count.increment();
	}
	static_self_probe_records().clear();
}

TEST_CASE("[Modules][FoundryScript][StaticSelf] Concurrent calls through different receivers stay independent") {
	StaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_static_self_source(
			"class Base:\n"
			"\tstatic func probe(cb: Callable) -> void:\n"
			"\t\tcb.call()\n"
			"\n"
			"class FirstDerived extends Base:\n"
			"\tpass\n"
			"\n"
			"class SecondDerived extends Base:\n"
			"\tpass\n");

	StaticSelfConcurrentCall first;
	first.receiver = static_self_subclass(script, SNAME("FirstDerived"));
	first.iteration_count = 128;
	StaticSelfConcurrentCall second;
	second.receiver = static_self_subclass(script, SNAME("SecondDerived"));
	second.iteration_count = 128;
	REQUIRE(first.receiver.is_valid());
	REQUIRE(second.receiver.is_valid());

	Thread first_thread;
	Thread second_thread;
	first_thread.start(run_static_self_concurrent_calls, &first);
	second_thread.start(run_static_self_concurrent_calls, &second);
	first_thread.wait_to_finish();
	second_thread.wait_to_finish();

	CHECK_EQ(first.mismatch_count.get(), 0);
	CHECK_EQ(second.mismatch_count.get(), 0);
	CHECK_EQ(first.completed_count.get(), first.iteration_count);
	CHECK_EQ(second.completed_count.get(), second.iteration_count);
}

} // namespace FSTests
