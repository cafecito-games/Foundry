/**************************************************************************/
/*  test_missing_static_self.h                                            */
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
#include "../fs_cache.h"
#include "../fs_compiler.h"
#include "../fs_function.h"
#include "../fs_parser.h"
#include "fs_test_language_lifecycle.h"

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/callable_method_pointer.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

// The missing-static-self diagnostic guards every instruction that has to re-bind a `Self`-marked
// descriptor to the frame's receiver and finds none. It is unreachable from Foundry Script source --
// every dispatch entry point supplies a live receiver, a static frame whose *signature* needs one is
// refused before its first instruction, and `FSCache` keeps a program from freeing a script it can
// name -- so the states it exists for can only be built from outside the language: a C++ caller that
// invokes `FSFunction::call()` with an absent or stale `FSStaticSelfContext`, or compiled bytecode
// whose constant pool marks a descriptor `is_self_type` (the verifier bounds-checks operands, not
// descriptor contents). That is why the path stays a reported error rather than an assertion, and why
// its coverage lives here rather than in a `.fs` fixture.
//
// Each case below constructs one of those states directly and asserts on what the run actually
// produced: the reported diagnostic, the value handed back, and -- where a callee could have run --
// that it did not.

namespace FSTests {

// Reaches the compiled constant pool so a test can install the operand shapes only untrusted compiled
// data can carry. A real compiler never emits either of these, which is exactly why the runtime guards
// that catch them have no other way to be exercised.
class TestMissingStaticSelfAccessor {
public:
	// Marks every type-descriptor constant of `p_function` as having come from `Self`. Returns how many
	// were marked so a caller can require that the instruction it targets actually has one.
	static int mark_type_descriptor_constants_self_referencing(FSFunction *p_function) {
		int marked = 0;
		for (int i = 0; i < p_function->constants.size(); i++) {
			if (p_function->constants[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary descriptor = p_function->constants[i];
			if (descriptor.is_typed_key() || !descriptor.has("builtin_type")) {
				continue;
			}
			descriptor["is_self_type"] = true;
			marked++;
		}
		return marked;
	}

	// Replaces the constant holding `p_object` with `p_replacement`, so an instruction whose class
	// operand the compiler folded to a live class runs against the value a corrupt constant pool could
	// have supplied instead.
	static bool replace_object_constant(FSFunction *p_function, const Object *p_object, const Variant &p_replacement) {
		for (int i = 0; i < p_function->constants.size(); i++) {
			if (p_function->constants[i].get_type() != Variant::OBJECT) {
				continue;
			}
			if (p_function->constants[i].get_validated_object() != p_object) {
				continue;
			}
			p_function->constants.write[i] = p_replacement;
			p_function->_constants_ptr = p_function->constants.ptrw();
			return true;
		}
		return false;
	}
};

// Set from inside a callee reached through a guarded call boundary, so a case can assert the callee
// did not observe an argument the boundary was supposed to refuse.
static bool &missing_static_self_callee_ran() {
	static thread_local bool ran = false;
	return ran;
}

static void missing_static_self_callee_probe() {
	missing_static_self_callee_ran() = true;
}

struct MissingStaticSelfLanguageScope {
	MissingStaticSelfLanguageScope() {
		FSTests::ensure_fs_language_initialized();
		missing_static_self_callee_ran() = false;
	}
	~MissingStaticSelfLanguageScope() {
		missing_static_self_callee_ran() = false;
	}
};

// Captures every engine error raised while a call runs, so a rejection can be asserted on by its
// reported reason rather than only by whether the call produced a value.
struct MissingStaticSelfErrorRecorder {
	MissingStaticSelfErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~MissingStaticSelfErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line,
			const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		MissingStaticSelfErrorRecorder *self = static_cast<MissingStaticSelfErrorRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	ErrorHandlerList handler;
	String messages;
};

// The source is written to disk and published through `FSCache` before it is compiled, the way
// production loading does, so a reduction that resolves a specialized handle finds the same script.
static Ref<FoundryScript> compile_missing_static_self_source(const String &p_source) {
	static int unique_index = 0;
	const String path = TestUtils::get_temp_path(vformat("test_missing_static_self_%d.fs", unique_index++));
	{
		Ref<FileAccess> source_file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(source_file.is_valid());
		source_file->store_string(p_source);
	}

	Error cache_error = OK;
	Ref<FoundryScript> script = FSCache::get_shallow_script(path, cache_error);
	REQUIRE(cache_error == OK);
	REQUIRE(script.is_valid());
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

static FSFunction *missing_static_self_function(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, FSFunction *>::ConstIterator element = p_script->get_member_functions().find(p_name);
	return element ? element->value : nullptr;
}

static Ref<FoundryScript> missing_static_self_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

// The diagnostic every guarded instruction reports. Kept as one constant so a case asserts on the
// diagnostic itself rather than on a paraphrase of it.
static const char *MISSING_STATIC_SELF_MESSAGE = "the running frame has no static receiver to resolve it against.";

// A valid descriptor whose backing script no longer resolves. A freshly instantiated, never-registered
// script released immediately stands in for a receiver freed between dispatch and execution: `FSCache`
// holds a strong reference to every script a program can name, so this state has no source-level form.
static FSStaticSelfContext missing_static_self_dead_receiver() {
	FSStaticSelfContext dead_receiver;
	{
		Ref<FoundryScript> transient;
		transient.instantiate();
		REQUIRE(transient.is_valid());
		dead_receiver = FSStaticSelfContext::for_script(transient);
		REQUIRE(dead_receiver.is_fully_live());
	}
	REQUIRE(dead_receiver.is_valid());
	REQUIRE_FALSE(dead_receiver.is_fully_live());
	return dead_receiver;
}

// Runs a static function the way a C++ caller with no receiver would: no instance, no `self` override,
// and no `FSStaticSelfContext`. That is the state the guard exists for, and the one no dispatch entry
// point in the engine produces.
static Variant call_without_static_receiver(FSFunction *p_function, const Vector<Variant> &p_arguments,
		Callable::CallError &r_error) {
	LocalVector<const Variant *> argument_pointers;
	argument_pointers.resize(p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		argument_pointers[i] = &p_arguments[i];
	}
	return p_function->call(nullptr, argument_pointers.ptr(), p_arguments.size(), r_error, nullptr, nullptr, nullptr);
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A receiver-less static frame refuses a Self-typed type test") {
	MissingStaticSelfLanguageScope language;

	// The signature is free of `Self`, so the frame is not refused before its first instruction and the
	// type test itself is what needs the receiver.
	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"static func matches(value: Variant) -> bool:\n"
			"\treturn value is Array[Self]\n");
	FSFunction *matches = missing_static_self_function(script, SNAME("matches"));
	REQUIRE(matches != nullptr);
	REQUIRE_FALSE(matches->has_self_referencing_signature());

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = call_without_static_receiver(matches, { Array() }, error);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	// The test is refused rather than answered against the class the declaration was lowered against,
	// so the frame hands back the return type's default.
	CHECK(result.get_type() == Variant::BOOL);
	CHECK_FALSE((bool)result);
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A dead receiver refuses a Self-typed store") {
	MissingStaticSelfLanguageScope language;

	// A typed local's store always carries its element descriptor to the run time, so a `Self`-typed
	// slot is re-bound to the frame's receiver on every assignment. The store is reached through an
	// instance frame because a `Self`-typed local can only be initialized from a statically exact
	// source, which in a receiver-less static frame is refused earlier for its own signature.
	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"class Holder:\n"
			"\tfunc keep(source: Array[Self], other: Array[Self]) -> int:\n"
			"\t\tvar items: Array[Self] = source\n"
			"\t\titems = other\n"
			"\t\treturn items.size()\n");
	const Ref<FoundryScript> holder = missing_static_self_subclass(script, SNAME("Holder"));
	REQUIRE(holder.is_valid());

	FSFunction *keep = missing_static_self_function(holder, SNAME("keep"));
	REQUIRE(keep != nullptr);

	Callable::CallError instantiate_error;
	const Variant holder_instance_variant = holder->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);
	Object *holder_instance = holder_instance_variant;
	REQUIRE(holder_instance != nullptr);
	FSInstance *holder_frame = static_cast<FSInstance *>(holder_instance->get_script_instance());
	REQUIRE(holder_frame != nullptr);

	FSStaticSelfContext dead_receiver = missing_static_self_dead_receiver();

	// Typed for the class the declaration was lowered against, which is what the fallback signature
	// expects, so the argument binding accepts it and the frame runs to its first store.
	ContainerType holder_element;
	holder_element.builtin_type = Variant::OBJECT;
	holder_element.class_name = SNAME("RefCounted");
	holder_element.script = holder;
	Array holder_array;
	holder_array.set_typed(holder_element);
	const Variant stored = holder_array;
	const Variant *arguments[2] = { &stored, &stored };

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = keep->call(holder_frame, arguments, 2, error, nullptr, nullptr, &dead_receiver);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	// Storing against the class the declaration was lowered against would leave the slot holding a
	// value the receiver's own type never admitted, so the frame is aborted and returns the default.
	CHECK(result.get_type() == Variant::INT);
	CHECK((int64_t)result == 0);
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A receiver-less static frame refuses a Self-typed call argument") {
	MissingStaticSelfLanguageScope language;

	// The analyzer never substitutes `Self` into a generic call's checked argument -- such a
	// substitution is left to the receiver-relative contract -- so the only way a
	// `VALIDATE_CALL_ARGUMENT` descriptor can carry the marker is compiled data the verifier does not
	// inspect. Marking the caller's descriptor constants installs exactly that.
	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"static func identity[T](value: T, callee_ran: Callable) -> T:\n"
			"\tcallee_ran.call()\n"
			"\treturn value\n"
			"\n"
			"static func run(value: Variant, callee_ran: Callable) -> int:\n"
			"\treturn identity[int](value, callee_ran)\n");
	FSFunction *run = missing_static_self_function(script, SNAME("run"));
	REQUIRE(run != nullptr);
	REQUIRE_FALSE(run->has_self_referencing_signature());
	REQUIRE(TestMissingStaticSelfAccessor::mark_type_descriptor_constants_self_referencing(run) > 0);

	const Variant callee_probe = Variant(callable_mp_static(&missing_static_self_callee_probe));

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = call_without_static_receiver(run, { Variant("not an int"), callee_probe }, error);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	// The argument boundary is refused before dispatch, so the callee never observes the value.
	CHECK_FALSE(missing_static_self_callee_ran());
	CHECK(result.get_type() == Variant::INT);
	CHECK((int64_t)result == 0);
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A receiver-less static frame refuses a Self-typed cast") {
	MissingStaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"static func narrow(value: Variant) -> Variant:\n"
			"\treturn value as Self\n");
	FSFunction *narrow = missing_static_self_function(script, SNAME("narrow"));
	REQUIRE(narrow != nullptr);
	REQUIRE_FALSE(narrow->has_self_referencing_signature());

	Ref<RefCounted> value;
	value.instantiate();

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = call_without_static_receiver(narrow, { value }, error);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	// A refused cast must not fall through to the uncast value.
	CHECK(result.get_type() == Variant::NIL);
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A receiver-less static frame refuses a Self-typed construction") {
	MissingStaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"static func build() -> int:\n"
			"\tvar items: Array[Self] = []\n"
			"\treturn items.size()\n");
	FSFunction *build = missing_static_self_function(script, SNAME("build"));
	REQUIRE(build != nullptr);
	REQUIRE_FALSE(build->has_self_referencing_signature());

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = call_without_static_receiver(build, {}, error);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	// Constructing the container against the declaring class would produce an `Array` typed for an
	// ancestor specialization, so the instruction is refused and the frame returns the default.
	CHECK(result.get_type() == Variant::INT);
	CHECK((int64_t)result == 0);
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A dead receiver refuses a Self-typed return") {
	MissingStaticSelfLanguageScope language;

	// An instance frame always has a `self`, so a signature it cannot resolve falls back to the declared
	// types rather than refusing the call. That is what lets the typed return be reached at all: the
	// return's own descriptor is still bound through the supplied receiver, and a receiver whose script
	// was freed between dispatch and execution has to be reported instead of silently standing in for
	// the class the declaration was lowered against.
	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"class Holder:\n"
			"\tfunc items(source: Array[Self]) -> Array[Self]:\n"
			"\t\treturn source\n");
	const Ref<FoundryScript> holder = missing_static_self_subclass(script, SNAME("Holder"));
	REQUIRE(holder.is_valid());

	FSFunction *items = missing_static_self_function(holder, SNAME("items"));
	REQUIRE(items != nullptr);
	REQUIRE(items->has_self_referencing_signature());

	Callable::CallError instantiate_error;
	const Variant holder_instance_variant = holder->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);
	Object *holder_instance = holder_instance_variant;
	REQUIRE(holder_instance != nullptr);
	FSInstance *holder_frame = static_cast<FSInstance *>(holder_instance->get_script_instance());
	REQUIRE(holder_frame != nullptr);

	FSStaticSelfContext dead_receiver = missing_static_self_dead_receiver();

	// Typed for the class the declaration was lowered against, which is what the fallback signature
	// expects, so the argument binding accepts it and the frame runs to its return.
	ContainerType holder_element;
	holder_element.builtin_type = Variant::OBJECT;
	holder_element.class_name = SNAME("RefCounted");
	holder_element.script = holder;
	Array holder_array;
	holder_array.set_typed(holder_element);
	const Variant returned = holder_array;
	const Variant *arguments[1] = { &returned };

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = items->call(holder_frame, arguments, 1, error, nullptr, nullptr, &dead_receiver);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	// The refused return hands back the return type's default, not the unvalidated value.
	CHECK(result.get_type() == Variant::ARRAY);
	CHECK(((Array)result).is_empty());
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A receiver-less static frame refuses Self in an expression position") {
	MissingStaticSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"static func class_handle() -> Variant:\n"
			"\tvar handle = Self\n"
			"\treturn handle\n");
	FSFunction *class_handle = missing_static_self_function(script, SNAME("class_handle"));
	REQUIRE(class_handle != nullptr);
	REQUIRE_FALSE(class_handle->has_self_referencing_signature());

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = call_without_static_receiver(class_handle, {}, error);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	// Producing the declaring class here would hand the program a handle that constructs the wrong
	// class, so nothing is produced at all.
	CHECK(result.get_type() == Variant::NIL);
}

TEST_CASE("[Modules][FoundryScript][MissingStaticSelf] A freed specialization argument reports its own condition") {
	MissingStaticSelfLanguageScope language;

	// A stale specialized handle is not a missing receiver: the frame may have one, and the failing
	// condition is the freed type-argument script. The construction site therefore reports that
	// condition rather than borrowing the `Self` diagnostic.
	const Ref<FoundryScript> script = compile_missing_static_self_source(
			"class Box[T]:\n"
			"\tvar value: T\n"
			"\n"
			"static func build() -> Variant:\n"
			"\treturn Box[int].new()\n");
	const Ref<FoundryScript> box = missing_static_self_subclass(script, SNAME("Box"));
	REQUIRE(box.is_valid());

	FSFunction *build = missing_static_self_function(script, SNAME("build"));
	REQUIRE(build != nullptr);

	// The handle keeps its own script strongly and its type arguments weakly, so releasing the argument
	// script leaves a handle that still names a specialization it can no longer describe.
	Ref<FSSpecializedClassHandle> stale_handle;
	{
		ContainerType stale_argument;
		stale_argument.builtin_type = Variant::OBJECT;
		stale_argument.class_name = SNAME("RefCounted");
		Ref<FoundryScript> transient;
		transient.instantiate();
		REQUIRE(transient.is_valid());
		stale_argument.script = transient;
		stale_handle = FSSpecializedClassHandle::create(box, { stale_argument });
	}
	REQUIRE(stale_handle.is_valid());
	REQUIRE_FALSE(stale_handle->is_fully_live());

	// The compiler folds the construction's class operand to the live `Box`; swapping in the stale
	// handle installs the operand only untrusted compiled data could supply.
	REQUIRE(TestMissingStaticSelfAccessor::replace_object_constant(build, box.ptr(), Variant(stale_handle)));

	MissingStaticSelfErrorRecorder recorder;
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = call_without_static_receiver(build, {}, error);
	ERR_PRINT_ON;

	CHECK(recorder.messages.contains("one of its type arguments names a script that has been freed."));
	// The `Self` diagnostic would name the wrong condition entirely: nothing here asked for a receiver.
	CHECK_FALSE(recorder.messages.contains(MISSING_STATIC_SELF_MESSAGE));
	CHECK(result.get_type() == Variant::NIL);
}

} //namespace FSTests
