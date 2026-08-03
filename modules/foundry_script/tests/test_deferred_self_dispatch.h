/**************************************************************************/
/*  test_deferred_self_dispatch.h                                         */
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
#include "../fs_class_handle_callable.h"
#include "../fs_compiler.h"
#include "../fs_function.h"
#include "../fs_parser.h"

#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

// `Self` when the call and its execution are separated in time: a callable extracted now and invoked
// later, and a coroutine that suspends and resumes. Both are asked what they *produced*, because a
// declared return type can read as the exact specialization while the constructed value was still
// built for an ancestor.

namespace FSTests {

struct DeferredSelfLanguageScope {
	DeferredSelfLanguageScope() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

// Published through `FSCache` before compiling, the way production loading does: reductions that
// resolve a specialized handle go back through the cache for the declaring file.
static Ref<FoundryScript> compile_deferred_self_source(const String &p_source) {
	static int unique_index = 0;
	const String path = TestUtils::get_temp_path(vformat("test_deferred_self_dispatch_%d.fs", unique_index++));
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

static Ref<FoundryScript> deferred_self_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

// Reads a static member off a class handle exactly as a script does, so what comes back is whatever
// the extraction path produces rather than a callable the test built itself.
static Callable extract_deferred_self_callable(Object *p_handle, const StringName &p_name) {
	bool valid = false;
	const Variant extracted = p_handle->get(p_name, &valid);
	REQUIRE(valid);
	REQUIRE(extracted.get_type() == Variant::CALLABLE);
	return extracted;
}

TEST_CASE("[Modules][FoundryScript][DeferredSelf] Callables extracted from different receivers do not interfere") {
	DeferredSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_deferred_self_source(
			"class Base:\n"
			"\tstatic func spawn() -> Self:\n"
			"\t\treturn Self.new()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const Ref<FoundryScript> base = deferred_self_subclass(script, SNAME("Base"));
	const Ref<FoundryScript> derived = deferred_self_subclass(script, SNAME("Derived"));
	REQUIRE(base.is_valid());
	REQUIRE(derived.is_valid());

	const Callable from_derived = extract_deferred_self_callable(derived.ptr(), SNAME("spawn"));
	const Callable from_base = extract_deferred_self_callable(base.ptr(), SNAME("spawn"));

	// Interleaved on purpose: a receiver stored on the shared `FSFunction` instead of on the extracted
	// callable would let whichever call ran last decide for both.
	const Ref<RefCounted> first_derived = from_derived.call();
	const Ref<RefCounted> first_base = from_base.call();
	const Ref<RefCounted> second_derived = from_derived.call();

	REQUIRE(first_derived.is_valid());
	REQUIRE(first_base.is_valid());
	REQUIRE(second_derived.is_valid());
	CHECK(first_derived->get_script() == Variant(derived));
	CHECK(first_base->get_script() == Variant(base));
	CHECK(second_derived->get_script() == Variant(derived));
}

TEST_CASE("[Modules][FoundryScript][DeferredSelf] A callable outliving its extraction scope keeps the specialization") {
	DeferredSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_deferred_self_source(
			"class Crate[T]:\n"
			"\tvar value: T\n"
			"\n"
			"\tstatic func spawn() -> Self:\n"
			"\t\treturn Self.new()\n");

	const Ref<FoundryScript> crate = deferred_self_subclass(script, SNAME("Crate"));
	REQUIRE(crate.is_valid());

	ContainerType image_argument;
	image_argument.builtin_type = Variant::OBJECT;
	image_argument.class_name = SNAME("ImageTexture");

	Callable extracted;
	{
		// The specialized handle is a transient value object, so this scope is the only thing holding
		// it. An extracted callable that only recorded its `ObjectID` would be dangling below.
		const Ref<FSSpecializedClassHandle> handle = FSSpecializedClassHandle::create(crate, { image_argument });
		REQUIRE(handle.is_valid());
		extracted = extract_deferred_self_callable(handle.ptr(), SNAME("spawn"));
	}

	CHECK(extracted.is_valid());
	const Ref<RefCounted> made = extracted.call();
	REQUIRE(made.is_valid());
	ScriptInstance *made_instance = made->get_script_instance();
	REQUIRE(made_instance != nullptr);

	Vector<ContainerType> reified_arguments;
	made_instance->get_reified_type_arguments(reified_arguments);
	REQUIRE_EQ(reified_arguments.size(), 1);
	CHECK(reified_arguments[0].builtin_type == Variant::OBJECT);
	CHECK(reified_arguments[0].class_name == SNAME("ImageTexture"));
}

TEST_CASE("[Modules][FoundryScript][DeferredSelf] A callable with no receiver refuses instead of falling back") {
	DeferredSelfLanguageScope language;

	// A missing receiver is a broken dispatch path. Dispatching through the unspecialized script the
	// handle would have represented is exactly the silent wrong answer this must not produce.
	const Callable without_receiver = Callable(memnew(FSClassHandleCallable(Ref<ClassHandle>(), SNAME("spawn"))));
	CHECK_FALSE(without_receiver.is_valid());

	Variant result;
	Callable::CallError error;
	without_receiver.callp(nullptr, 0, result, error);
	CHECK(error.error == Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL);
	CHECK(result.get_type() == Variant::NIL);
}

TEST_CASE("[Modules][FoundryScript][DeferredSelf] A resumed call constructs the receiver it began on") {
	DeferredSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_deferred_self_source(
			"class Base:\n"
			"\tstatic async func spawn(source: Object) -> Self:\n"
			"\t\tawait source.script_changed\n"
			"\t\treturn Self.new()\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const Ref<FoundryScript> derived = deferred_self_subclass(script, SNAME("Derived"));
	REQUIRE(derived.is_valid());

	Object *signal_source = memnew(Object);

	const Variant source_argument = Variant(signal_source);
	const Variant *arguments[1] = { &source_argument };
	Callable::CallError error;
	Object *receiver = derived.ptr();
	const Variant suspended = receiver->callp(SNAME("spawn"), arguments, 1, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	Ref<FSFunctionState> function_state = suspended;
	REQUIRE(function_state.is_valid());

	// The object is built after the suspension, so it can only be right if the resumed frame still
	// knows which handle the call began on.
	const Ref<RefCounted> made = function_state->resume();
	REQUIRE(made.is_valid());
	CHECK(made->get_script() == Variant(derived));

	memdelete(signal_source);
}

} //namespace FSTests
