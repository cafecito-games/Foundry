/**************************************************************************/
/*  test_runtime_self_resolution.h                                        */
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
#include "../fs_conformance_registry.h"
#include "../fs_function.h"
#include "../fs_lambda_callable.h"
#include "../fs_parser.h"

#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

// The observable half of late-bound `Self`: what a call actually produces, inspected on the returned
// object rather than on the function's declared signature. A declared return type can read as the
// exact specialization while the value it describes was still built for an ancestor, so every case
// here asks the value itself -- its class, and the concrete arguments it was reified with.

namespace FSTests {

struct RuntimeSelfLanguageScope {
	RuntimeSelfLanguageScope() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

// Drops the retroactive conformances a source registered so a later case cannot dispatch through a
// witness belonging to a script this one compiled.
struct RuntimeSelfConformanceScope {
	String path;

	explicit RuntimeSelfConformanceScope(const String &p_path) :
			path(p_path) {}
	~RuntimeSelfConformanceScope() {
		FSConformanceRegistry::get_singleton()->clear_file(path);
		FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(path);
	}
};

// The source is written to disk and published through `FSCache` before it is compiled, the way
// production loading does: reductions that resolve a specialized handle such as `Crate[Self]` go back
// through the cache for the declaring file and must find the same script.
static Ref<FoundryScript> compile_runtime_self_source(const String &p_source) {
	static int unique_index = 0;
	const String path = TestUtils::get_temp_path(vformat("test_runtime_self_resolution_%d.fs", unique_index++));
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

// Calls `p_method` the way any engine caller would, so the receiver's own dispatch entry point is
// what decides which receiver the frame is handed.
static Variant call_runtime_self_handle(Object *p_receiver, const StringName &p_method,
		const Vector<Variant> &p_arguments, Callable::CallError &r_error) {
	LocalVector<const Variant *> argument_pointers;
	argument_pointers.resize(p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		argument_pointers[i] = &p_arguments[i];
	}
	return p_receiver->callp(p_method, argument_pointers.ptr(), p_arguments.size(), r_error);
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A native witness constructs the exact receiver") {
	RuntimeSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_runtime_self_source(
			"trait Factory:\n"
			"\tabstract static func make() -> Self\n"
			"\n"
			"extend RefCounted uses Factory:\n"
			"\tstatic func make() -> Self:\n"
			"\t\treturn Self.new()\n");
	RuntimeSelfConformanceScope conformance_scope(script->get_script_path());

	const HashMap<StringName, int> &global_map = FSLanguage::get_singleton()->get_global_map();
	REQUIRE(global_map.has(SNAME("Resource")));
	Object *resource_handle = FSLanguage::get_singleton()->get_global_array()[global_map[SNAME("Resource")]];
	REQUIRE(resource_handle != nullptr);

	Callable::CallError error;
	const Variant made = call_runtime_self_handle(resource_handle, SNAME("make"), {}, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	// The witness was declared for `RefCounted`; the value has to be the class the call was made
	// through, not the conformance target it was compiled against.
	const Ref<RefCounted> made_object = made;
	REQUIRE(made_object.is_valid());
	CHECK(made_object->get_class_name() == SNAME("Resource"));
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A specialized handle keeps the exact receiver as its argument") {
	RuntimeSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_runtime_self_source(
			"class Crate[T]:\n"
			"\tvar value: T\n"
			"\n"
			"trait Packing:\n"
			"\tabstract static func pack(value: Self) -> Crate[Self]\n"
			"\n"
			"extend Resource uses Packing:\n"
			"\tstatic func pack(value: Self) -> Crate[Self]:\n"
			"\t\tvar crate := Crate[Self].new()\n"
			"\t\tcrate.value = value\n"
			"\t\treturn crate\n");
	RuntimeSelfConformanceScope conformance_scope(script->get_script_path());

	const HashMap<StringName, int> &global_map = FSLanguage::get_singleton()->get_global_map();
	REQUIRE(global_map.has(SNAME("ImageTexture")));
	Object *image_texture_handle = FSLanguage::get_singleton()->get_global_array()[global_map[SNAME("ImageTexture")]];
	REQUIRE(image_texture_handle != nullptr);

	Ref<ImageTexture> image;
	image.instantiate();

	Callable::CallError error;
	const Variant packed = call_runtime_self_handle(image_texture_handle, SNAME("pack"), { image }, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	const Ref<RefCounted> crate = packed;
	REQUIRE(crate.is_valid());
	ScriptInstance *crate_instance = crate->get_script_instance();
	REQUIRE(crate_instance != nullptr);

	// Reading the reified arguments off the constructed object is the point: a runtime `Crate[Resource]`
	// or `Crate[Variant]` is wrong even though the call's declared return type looks specialized.
	Vector<ContainerType> reified_arguments;
	crate_instance->get_reified_type_arguments(reified_arguments);
	REQUIRE_EQ(reified_arguments.size(), 1);
	CHECK(reified_arguments[0].builtin_type == Variant::OBJECT);
	CHECK(reified_arguments[0].class_name == SNAME("ImageTexture"));

	// And the specialization is enforced, not merely recorded.
	Ref<Material> unrelated;
	unrelated.instantiate();
	ERR_PRINT_OFF;
	const bool stored = crate_instance->set(SNAME("value"), unrelated);
	ERR_PRINT_ON;
	CHECK_FALSE(stored);
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A static call with no receiver is refused, not approximated") {
	RuntimeSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_runtime_self_source(
			"trait Echoing:\n"
			"\tabstract static func echo(value: Self) -> Self\n"
			"\n"
			"extend RefCounted uses Echoing:\n"
			"\tstatic func echo(value: Self) -> Self:\n"
			"\t\treturn value\n");
	RuntimeSelfConformanceScope conformance_scope(script->get_script_path());

	FSFunction *witness = FSConformanceRegistry::get_singleton()->find_native_witness_function(
			SNAME("RefCounted"), SNAME("echo"));
	REQUIRE(witness != nullptr);
	REQUIRE(witness->has_self_referencing_signature());

	Ref<RefCounted> value;
	value.instantiate();
	const Variant argument = value;
	const Variant *arguments[1] = { &argument };

	// Invoking without a receiver is a broken dispatch path. Falling back to the conformance target
	// would let the call succeed and hide it, so the call is refused instead.
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = witness->call(nullptr, arguments, 1, error, nullptr, nullptr, nullptr);
	ERR_PRINT_ON;
	CHECK(error.error == Callable::CallError::CALL_ERROR_INVALID_METHOD);
	CHECK(result.get_type() == Variant::NIL);
}

// A lambda created in a static receiver context captures that receiver the same way an extracted
// static callable does, and the captured value travels with the callable after the creating frame is
// gone. These cases inspect the constructed object's exact script rather than the lambda's declared
// return type: a `Variant` or declaring-class substitution reads back as the right type while still
// constructing the wrong object.

static Ref<FoundryScript> runtime_self_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

// Invokes a Callable returned by a static factory and returns the constructed object, failing the
// case clearly if the factory or the lambda itself reported an error.
static Ref<RefCounted> invoke_runtime_self_factory(const Variant &p_factory_variant) {
	REQUIRE(p_factory_variant.get_type() == Variant::CALLABLE);
	Callable factory = p_factory_variant;
	REQUIRE(factory.is_valid());

	Variant produced;
	Callable::CallError invoke_error;
	factory.callp(nullptr, 0, produced, invoke_error);
	REQUIRE(invoke_error.error == Callable::CallError::CALL_OK);
	const Ref<RefCounted> produced_object = produced;
	REQUIRE(produced_object.is_valid());
	return produced_object;
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A static lambda captures and retains the exact script receiver") {
	RuntimeSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_runtime_self_source(
			"class Base:\n"
			"\tstatic func make_factory() -> Callable:\n"
			"\t\treturn func() -> Self:\n"
			"\t\t\treturn Self.new()\n"
			"\n"
			"class Child extends Base:\n"
			"\tpass\n"
			"\n"
			"class Sibling extends Base:\n"
			"\tpass\n");
	const Ref<FoundryScript> base = runtime_self_subclass(script, SNAME("Base"));
	REQUIRE(base.is_valid());
	const Ref<FoundryScript> child = runtime_self_subclass(script, SNAME("Child"));
	REQUIRE(child.is_valid());
	const Ref<FoundryScript> sibling = runtime_self_subclass(script, SNAME("Sibling"));
	REQUIRE(sibling.is_valid());

	// The mandatory escape path: the outer static frame is gone before each callable runs. Each must
	// construct the receiver it was created through, never the declaring class.
	Callable::CallError error;
	const Variant child_factory = call_runtime_self_handle(child.ptr(), SNAME("make_factory"), {}, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	const Variant base_factory = call_runtime_self_handle(base.ptr(), SNAME("make_factory"), {}, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	const Variant sibling_factory = call_runtime_self_handle(sibling.ptr(), SNAME("make_factory"), {}, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	const Ref<RefCounted> child_made = invoke_runtime_self_factory(child_factory);
	const Ref<RefCounted> base_made = invoke_runtime_self_factory(base_factory);
	const Ref<RefCounted> sibling_made = invoke_runtime_self_factory(sibling_factory);

	// Exact identity, read off the value: `Child`/`Base`/`Sibling`, not a common ancestor.
	CHECK(child_made->get_script_instance()->get_script()->get_instance_id() == child->get_instance_id());
	CHECK(base_made->get_script_instance()->get_script()->get_instance_id() == base->get_instance_id());
	CHECK(sibling_made->get_script_instance()->get_script()->get_instance_id() == sibling->get_instance_id());

	// Sibling independence under interleaved invocation: resolving one receiver never contaminates
	// another escaped callable created through a different handle.
	CHECK(invoke_runtime_self_factory(child_factory)->get_script_instance()->get_script()->get_instance_id() == child->get_instance_id());
	CHECK(invoke_runtime_self_factory(sibling_factory)->get_script_instance()->get_script()->get_instance_id() == sibling->get_instance_id());

	// A static lambda is NOT an instance-self lambda: the analyzer must not set `use_self` just
	// because the lambda references `Self`. That flag routes through `FSLambdaSelfCallable`, whose
	// receiver is an instance object a static frame does not have.
	REQUIRE_FALSE(base->get_lambda_info().is_empty());
	const FoundryScript::LambdaInfo &lambda_info = base->get_lambda_info().begin()->value;
	CHECK_FALSE(lambda_info.use_self);
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A static lambda created by a native witness captures that receiver") {
	RuntimeSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_runtime_self_source(
			"trait Factory:\n"
			"\tabstract static func make_factory() -> Callable\n"
			"\n"
			"extend RefCounted uses Factory:\n"
			"\tstatic func make_factory() -> Callable:\n"
			"\t\treturn func() -> Self:\n"
			"\t\t\treturn Self.new()\n");
	RuntimeSelfConformanceScope conformance_scope(script->get_script_path());

	const HashMap<StringName, int> &global_map = FSLanguage::get_singleton()->get_global_map();
	REQUIRE(global_map.has(SNAME("Resource")));
	Object *resource_handle = FSLanguage::get_singleton()->get_global_array()[global_map[SNAME("Resource")]];
	REQUIRE(resource_handle != nullptr);

	Callable::CallError error;
	const Variant factory = call_runtime_self_handle(resource_handle, SNAME("make_factory"), {}, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	const Ref<RefCounted> made = invoke_runtime_self_factory(factory);
	REQUIRE(made.is_valid());
	// The witness was declared for `RefCounted`; the value must be the class the call was made
	// through, not the conformance target it was compiled against.
	CHECK(made->get_class_name() == SNAME("Resource"));
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A static lambda with no captured receiver is refused, not approximated") {
	RuntimeSelfLanguageScope language;

	// The lambda's signature references `Self`, so it can only run with a receiver. Constructing the
	// ordinary callable directly with no captured context simulates a callable that escaped a frame
	// which never had a receiver to give (e.g. a low-level dispatch path), and verifies the failure
	// surfaces instead of a fallback to the owning class, the conformance target, or `Variant`.
	const Ref<FoundryScript> script = compile_runtime_self_source(
			"static func make_echo() -> Callable:\n"
			"\treturn func(value: Self) -> Self:\n"
			"\t\treturn value\n");
	REQUIRE_FALSE(script->get_lambda_info().is_empty());
	FSFunction *echo_lambda = script->get_lambda_info().begin()->key;
	REQUIRE(echo_lambda != nullptr);

	FSLambdaCallable callable(script, echo_lambda, {});

	Variant result;
	Callable::CallError error;
	ERR_PRINT_OFF;
	callable.call(nullptr, 0, result, error);
	ERR_PRINT_ON;
	CHECK(error.error == Callable::CallError::CALL_ERROR_INVALID_METHOD);
	CHECK(result.get_type() == Variant::NIL);
}

// A freshly instantiated FoundryScript whose only strong reference is the caller's. Unlike a compiled
// script (cached by path) or a nested class (held by its parent's subclass map), dropping this
// reference actually frees it, which is what the freed-receiver diagnostic below depends on.
static Ref<FoundryScript> runtime_self_uncached_receiver() {
	Ref<FoundryScript> script;
	script.instantiate();
	return script;
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A static lambda whose signature references Self is refused when the captured receiver is freed") {
	RuntimeSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_runtime_self_source(
			"static func make_echo() -> Callable:\n"
			"\treturn func(value: Self) -> Self:\n"
			"\t\treturn value\n");
	REQUIRE_FALSE(script->get_lambda_info().is_empty());
	FSFunction *echo_lambda = script->get_lambda_info().begin()->key;
	REQUIRE(echo_lambda != nullptr);

	Ref<FoundryScript> receiver_script = runtime_self_uncached_receiver();
	REQUIRE(receiver_script.is_valid());
	FSStaticSelfContext receiver = FSStaticSelfContext::for_script(receiver_script);
	REQUIRE(receiver.is_fully_live());

	FSLambdaCallable callable(script, echo_lambda, {}, &receiver);

	// Dropping the last strong reference frees the receiver script before the lambda is ever invoked.
	receiver_script = Ref<FoundryScript>();
	REQUIRE_FALSE(receiver.is_fully_live());

	Variant argument = Variant();
	const Variant *arguments[] = { &argument };
	Variant result;
	Callable::CallError error;
	ERR_PRINT_OFF;
	callable.call(arguments, 1, result, error);
	ERR_PRINT_ON;
	CHECK(error.error == Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL);
	CHECK(result.get_type() == Variant::NIL);
}

TEST_CASE("[Modules][FoundryScript][RuntimeSelf] A static lambda whose body only references Self is refused when the captured receiver is freed") {
	RuntimeSelfLanguageScope language;

	// The signature (`-> RefCounted`) never mentions `Self`; only the body does, through `Self.new()`.
	// Before this fix, `resolve_signature_self` had nothing to fail fast on and the freed receiver was
	// only diagnosed deep inside `OPCODE_LOAD_STATIC_SELF_CLASS`, which sets `err_text` but never
	// `r_call_error.error`, so the call silently reported success with a NIL result.
	const Ref<FoundryScript> script = compile_runtime_self_source(
			"static func make_factory() -> Callable:\n"
			"\treturn func() -> RefCounted:\n"
			"\t\treturn Self.new()\n");
	REQUIRE_FALSE(script->get_lambda_info().is_empty());
	FSFunction *factory_lambda = script->get_lambda_info().begin()->key;
	REQUIRE(factory_lambda != nullptr);

	Ref<FoundryScript> receiver_script = runtime_self_uncached_receiver();
	REQUIRE(receiver_script.is_valid());
	FSStaticSelfContext receiver = FSStaticSelfContext::for_script(receiver_script);
	REQUIRE(receiver.is_fully_live());

	FSLambdaCallable callable(script, factory_lambda, {}, &receiver);

	// Dropping the last strong reference frees the receiver script before the lambda is ever invoked.
	receiver_script = Ref<FoundryScript>();
	REQUIRE_FALSE(receiver.is_fully_live());

	Variant result;
	Callable::CallError error;
	ERR_PRINT_OFF;
	callable.call(nullptr, 0, result, error);
	ERR_PRINT_ON;
	CHECK(error.error == Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL);
	CHECK(result.get_type() == Variant::NIL);
}

// The instance counterpart of the static-frame cases above. An inherited instance method has no
// compiled-in receiver either, so the running instance is its receiver: the leaf script the instance
// was created from, including its reified generic arguments. A `Self`-typed parameter is validated
// against that leaf, so a value that is legal for the declaring class but not for the receiver is
// rejected at the call frame -- the way any engine caller observes it through `Object::call`.
TEST_CASE("[Modules][FoundryScript][RuntimeSelf] An instance frame resolves Self to the receiver's leaf script") {
	RuntimeSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_runtime_self_source(
			"class Base:\n"
			"\tfunc consume(other: Self) -> void:\n"
			"\t\tpass\n"
			"\n"
			"class Child extends Base:\n"
			"\tpass\n"
			"\n"
			"class Sibling extends Base:\n"
			"\tpass\n");
	const Ref<FoundryScript> base = runtime_self_subclass(script, SNAME("Base"));
	REQUIRE(base.is_valid());
	const Ref<FoundryScript> child = runtime_self_subclass(script, SNAME("Child"));
	REQUIRE(child.is_valid());
	const Ref<FoundryScript> sibling = runtime_self_subclass(script, SNAME("Sibling"));
	REQUIRE(sibling.is_valid());

	Callable::CallError instantiate_error;
	const Variant child_instance_variant = child->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);
	Object *child_instance = child_instance_variant;
	REQUIRE(child_instance != nullptr);

	const Variant child_argument_variant = child->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);
	const Variant sibling_argument_variant = sibling->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);

	// A `Child` receiver resolves the `Self` parameter to `Child`. A `Child` argument is accepted,
	// and a `Sibling` -- legal for the declaring `Base` but not for the receiver's leaf -- is
	// rejected at the call frame rather than silently accepted.
	Callable::CallError accept_error;
	call_runtime_self_handle(child_instance, SNAME("consume"), { child_argument_variant }, accept_error);
	CHECK(accept_error.error == Callable::CallError::CALL_OK);

	ERR_PRINT_OFF;
	call_runtime_self_handle(child_instance, SNAME("consume"), { sibling_argument_variant }, accept_error);
	ERR_PRINT_ON;
	CHECK(accept_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);

	// A non-generic leaf fixed to a concrete argument resolves `Self` to the leaf, not the generic
	// base. Assert on the produced value's reified argument vector (not the declared signature): a
	// `Variant` or base-class substitution would read back as the right script while erasing the slot.
	const Ref<FoundryScript> crate_script = compile_runtime_self_source(
			"class Crate[T]:\n"
			"\tvar value: T\n"
			"\tfunc accept(other: Self) -> bool:\n"
			"\t\treturn other != null\n"
			"\n"
			"class IntCrate extends Crate[int]:\n"
			"\t\tpass\n");
	const Ref<FoundryScript> crate = runtime_self_subclass(crate_script, SNAME("Crate"));
	REQUIRE(crate.is_valid());
	const Ref<FoundryScript> int_crate = runtime_self_subclass(crate_script, SNAME("IntCrate"));
	REQUIRE(int_crate.is_valid());

	// A specialized generic receiver carries its concrete argument on the instance. Reading it back
	// off the value -- not the declared signature -- catches a `Variant` or unspecialized
	// substitution that would otherwise read back as the right script while erasing the slot.
	ContainerType int_container_argument;
	int_container_argument.builtin_type = Variant::INT;
	const Variant crate_int_instance_variant = crate->_new_specialized(nullptr, 0,
			{ int_container_argument }, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);
	Object *crate_int_instance = crate_int_instance_variant;
	REQUIRE(crate_int_instance != nullptr);
	ScriptInstance *crate_int_script_instance = crate_int_instance->get_script_instance();
	REQUIRE(crate_int_script_instance != nullptr);
	Vector<ContainerType> reified_arguments;
	crate_int_script_instance->get_reified_type_arguments(reified_arguments);
	REQUIRE_EQ(reified_arguments.size(), 1);
	CHECK(reified_arguments[0].builtin_type == Variant::INT);

	// A non-generic leaf fixed to `int` resolves `Self` to the leaf itself, not `Crate[int]`. An
	// `IntCrate` is accepted for `Self` on an `IntCrate` receiver...
	const Variant int_crate_instance_variant = int_crate->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);
	Object *int_crate_instance = int_crate_instance_variant;
	REQUIRE(int_crate_instance != nullptr);

	const Variant int_crate_argument_variant = int_crate->_new(nullptr, -1, instantiate_error);
	REQUIRE(instantiate_error.error == Callable::CallError::CALL_OK);

	Callable::CallError leaf_accept_error;
	call_runtime_self_handle(int_crate_instance, SNAME("accept"), { int_crate_argument_variant }, leaf_accept_error);
	CHECK(leaf_accept_error.error == Callable::CallError::CALL_OK);

	// ...and a `Crate[int]` -- the generic base of `IntCrate`, not an `IntCrate` itself -- is
	// rejected, because `Self` on the `IntCrate` receiver is `IntCrate`, not `Crate[int]`.
	ERR_PRINT_OFF;
	call_runtime_self_handle(int_crate_instance, SNAME("accept"), { crate_int_instance_variant }, leaf_accept_error);
	ERR_PRINT_ON;
	CHECK(leaf_accept_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
}

} //namespace FSTests
