/**************************************************************************/
/*  test_symbolic_self_lowering.h                                         */
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
#include "../fs_parser.h"
#include "fs_test_language_lifecycle.h"

#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

// A static call frame receives its exact receiver, but it can only act on that receiver if the
// lowered type metadata still says which positions came from `Self`. These cases pin the symbolic
// marker down: it has to survive analysis and lowering transitively -- through specialized generic
// arguments, typed containers, and class handles -- and it must never appear on a position the
// author genuinely wrote as the conformance target.
//
// No substitution happens here. Resolving the marker against the call-frame receiver is a separate
// concern; these cases only prove the information needed to do so is still present.

namespace FSTests {

struct SymbolicSelfLanguageScope {
	SymbolicSelfLanguageScope() {
		FSTests::ensure_fs_language_initialized();
	}
};

// Drops the retroactive conformances a source registered so a later case cannot dispatch through a
// witness belonging to a script this one compiled.
struct SymbolicSelfConformanceScope {
	String path;

	explicit SymbolicSelfConformanceScope(const String &p_path) :
			path(p_path) {}
	~SymbolicSelfConformanceScope() {
		FSConformanceRegistry::get_singleton()->clear_file(path);
		FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(path);
	}
};

// The source is written to disk and published through `FSCache` before it is compiled, the way
// production loading does: analyzer reductions that resolve a specialized handle such as
// `Crate[Self]` go back through the cache for the declaring file and must find the same script.
static Ref<FoundryScript> compile_symbolic_self_source(const String &p_source) {
	static int unique_index = 0;
	const String path = TestUtils::get_temp_path(vformat("test_symbolic_self_lowering_%d.fs", unique_index++));
	{
		Ref<FileAccess> source_file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(source_file.is_valid());
		source_file->store_string(p_source);
	}

	Error error = OK;
	Ref<FoundryScript> script = FSCache::get_shallow_script(path, error);
	REQUIRE(error == OK);
	REQUIRE(script.is_valid());

	FSParser parser;
	REQUIRE(parser.parse(p_source, path, false) == OK);

	FSAnalyzer analyzer(&parser);
	String reported_errors;
	const Error analyze_result = analyzer.analyze();
	for (const FSParser::ParserError &parse_error : parser.get_errors()) {
		reported_errors += "\n" + parse_error.message;
	}
	REQUIRE_MESSAGE(analyze_result == OK, reported_errors);

	FSCompiler compiler;
	REQUIRE(compiler.compile(&parser, script.ptr(), false) == OK);

	return script;
}

static FSFunction *symbolic_self_member_function(const Ref<FoundryScript> &p_script, const StringName &p_class,
		const StringName &p_method) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_class);
	if (!element) {
		return nullptr;
	}
	const HashMap<StringName, FSFunction *>::ConstIterator function = element->value->get_member_functions().find(p_method);
	return function ? function->value : nullptr;
}

static const FSDataType *symbolic_self_argument_type(const FSFunction *p_function, int p_index) {
	if (p_function == nullptr || p_index < 0 || p_index >= p_function->get_argument_count()) {
		return nullptr;
	}
	return &p_function->get_argument_type(p_index);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A bare Self parameter and return keep the marker") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Base:\n"
			"\tstatic func echo(value: Self) -> Self:\n"
			"\t\treturn value\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n");

	const FSFunction *echo = symbolic_self_member_function(script, SNAME("Base"), SNAME("echo"));
	REQUIRE(echo != nullptr);

	const FSDataType *parameter = symbolic_self_argument_type(echo, 0);
	REQUIRE(parameter != nullptr);
	CHECK(parameter->is_self_type);
	CHECK(echo->get_return_type().is_self_type);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A native witness keeps the marker instead of the target") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"trait Echoing:\n"
			"\tabstract static func echo(value: Self) -> Self\n"
			"\n"
			"extend RefCounted uses Echoing:\n"
			"\tstatic func echo(value: Self) -> Self:\n"
			"\t\treturn value\n");
	SymbolicSelfConformanceScope conformance_scope(script->get_script_path());

	const FSFunction *witness = FSConformanceRegistry::get_singleton()->find_native_witness_function(
			SNAME("RefCounted"), SNAME("echo"));
	REQUIRE(witness != nullptr);

	const FSDataType *parameter = symbolic_self_argument_type(witness, 0);
	REQUIRE(parameter != nullptr);
	// Bound to the conformance target for declaration-time validation, but still recognizable as a
	// position the resolver has to re-bind to the exact call-frame receiver.
	CHECK(parameter->native_type == SNAME("RefCounted"));
	CHECK(parameter->is_self_type);
	CHECK(witness->get_return_type().native_type == SNAME("RefCounted"));
	CHECK(witness->get_return_type().is_self_type);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A witness on a script target keeps the marker") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Target:\n"
			"\tpass\n"
			"\n"
			"trait Echoing:\n"
			"\tabstract static func echo(value: Self) -> Self\n"
			"\n"
			"extend Target uses Echoing:\n"
			"\tstatic func echo(value: Self) -> Self:\n"
			"\t\treturn value\n");
	SymbolicSelfConformanceScope conformance_scope(script->get_script_path());

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator target = script->get_subclasses().find(SNAME("Target"));
	REQUIRE(target);
	const FSFunction *witness = FSConformanceRegistry::get_singleton()->find_witness_function_for_target(
			target->value.ptr(), SNAME("echo"));
	REQUIRE(witness != nullptr);

	const FSDataType *parameter = symbolic_self_argument_type(witness, 0);
	REQUIRE(parameter != nullptr);
	CHECK(parameter->is_self_type);
	CHECK(witness->get_return_type().is_self_type);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A builtin witness keeps the marker") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"trait Echoing:\n"
			"\tabstract static func echo(value: Self) -> Self\n"
			"\n"
			"extend Vector2 uses Echoing:\n"
			"\tstatic func echo(value: Self) -> Self:\n"
			"\t\treturn value\n");
	SymbolicSelfConformanceScope conformance_scope(script->get_script_path());

	const FSFunction *witness = FSConformanceRegistry::get_singleton()->find_builtin_witness_function(
			Variant::VECTOR2, SNAME("echo"));
	REQUIRE(witness != nullptr);

	const FSDataType *parameter = symbolic_self_argument_type(witness, 0);
	REQUIRE(parameter != nullptr);
	CHECK(parameter->builtin_type == Variant::VECTOR2);
	CHECK(parameter->is_self_type);
	CHECK(witness->get_return_type().is_self_type);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A nested generic argument keeps the marker at depth") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Crate[T]:\n"
			"\tvar value: T\n"
			"\n"
			"class Base:\n"
			"\tstatic func packed() -> Crate[Self]:\n"
			"\t\treturn Crate[Self].new()\n"
			"\n"
			"\tstatic func nested() -> Array[Crate[Self]]:\n"
			"\t\treturn []\n");

	const FSFunction *packed = symbolic_self_member_function(script, SNAME("Base"), SNAME("packed"));
	REQUIRE(packed != nullptr);
	const FSDataType &crate_type = packed->get_return_type();
	CHECK_FALSE(crate_type.is_self_type);
	REQUIRE_EQ(crate_type.type_arguments.size(), 1);
	CHECK(crate_type.type_arguments[0].is_self_type);

	const FSFunction *nested = symbolic_self_member_function(script, SNAME("Base"), SNAME("nested"));
	REQUIRE(nested != nullptr);
	const FSDataType &array_type = nested->get_return_type();
	REQUIRE_EQ(array_type.container_element_types.size(), 1);
	REQUIRE_EQ(array_type.container_element_types[0].type_arguments.size(), 1);
	CHECK(array_type.container_element_types[0].type_arguments[0].is_self_type);
	CHECK(array_type.references_self_type());
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] Typed containers keep the marker in every slot") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Base:\n"
			"\tstatic func elements() -> Array[Self]:\n"
			"\t\treturn []\n"
			"\n"
			"\tstatic func values() -> Dictionary[String, Self]:\n"
			"\t\treturn {}\n"
			"\n"
			"\tstatic func keys() -> Dictionary[Self, String]:\n"
			"\t\treturn {}\n");

	const FSFunction *elements = symbolic_self_member_function(script, SNAME("Base"), SNAME("elements"));
	REQUIRE(elements != nullptr);
	REQUIRE_EQ(elements->get_return_type().container_element_types.size(), 1);
	CHECK(elements->get_return_type().container_element_types[0].is_self_type);

	const FSFunction *values = symbolic_self_member_function(script, SNAME("Base"), SNAME("values"));
	REQUIRE(values != nullptr);
	REQUIRE_EQ(values->get_return_type().container_element_types.size(), 2);
	CHECK_FALSE(values->get_return_type().container_element_types[0].is_self_type);
	CHECK(values->get_return_type().container_element_types[1].is_self_type);

	const FSFunction *keys = symbolic_self_member_function(script, SNAME("Base"), SNAME("keys"));
	REQUIRE(keys != nullptr);
	REQUIRE_EQ(keys->get_return_type().container_element_types.size(), 2);
	CHECK(keys->get_return_type().container_element_types[0].is_self_type);
	CHECK_FALSE(keys->get_return_type().container_element_types[1].is_self_type);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A class handle keeps the marker") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Base:\n"
			"\tstatic func handle(value: Type[Self]) -> Type[Self]:\n"
			"\t\treturn value\n");

	const FSFunction *handle = symbolic_self_member_function(script, SNAME("Base"), SNAME("handle"));
	REQUIRE(handle != nullptr);

	const FSDataType *parameter = symbolic_self_argument_type(handle, 0);
	REQUIRE(parameter != nullptr);
	CHECK(parameter->is_type_handle);
	CHECK(parameter->is_self_type);
	CHECK(handle->get_return_type().is_type_handle);
	CHECK(handle->get_return_type().is_self_type);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A rest parameter makes a signature receiver-dependent") {
	SymbolicSelfLanguageScope language;

	// A caller with no receiver descriptor to offer must refuse the whole signature, so the rest tail
	// counts the same as a declared parameter.
	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Base:\n"
			"\tstatic func collect(...values: Array[Self]) -> long:\n"
			"\t\treturn values.size()\n"
			"\n"
			"\tstatic func count(...values: Array[int]) -> long:\n"
			"\t\treturn values.size()\n");

	const FSFunction *collect = symbolic_self_member_function(script, SNAME("Base"), SNAME("collect"));
	REQUIRE(collect != nullptr);
	REQUIRE_EQ(collect->get_rest_parameter_type().container_element_types.size(), 1);
	CHECK(collect->get_rest_parameter_type().container_element_types[0].is_self_type);
	CHECK(collect->has_self_referencing_signature());

	const FSFunction *count = symbolic_self_member_function(script, SNAME("Base"), SNAME("count"));
	REQUIRE(count != nullptr);
	CHECK_FALSE(count->has_self_referencing_signature());
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A rest-only Self signature is receiver-dependent at call time") {
	SymbolicSelfLanguageScope language;

	// The call path reads the cached mirror, not `has_self_referencing_signature()` recomputed on
	// demand, so the cache has to be refreshed after the rest tail is installed on the compiled
	// function. A stale cache silently skips `Self` resolution for the whole frame.
	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Base:\n"
			"\tstatic func collect(...values: Array[Self]) -> long:\n"
			"\t\treturn values.size()\n"
			"\n"
			"\tstatic func count(...values: Array[int]) -> long:\n"
			"\t\treturn values.size()\n");

	const FSFunction *collect = symbolic_self_member_function(script, SNAME("Base"), SNAME("collect"));
	REQUIRE(collect != nullptr);
	CHECK(collect->is_receiver_dependent_at_call_time());

	const FSFunction *count = symbolic_self_member_function(script, SNAME("Base"), SNAME("count"));
	REQUIRE(count != nullptr);
	CHECK_FALSE(count->is_receiver_dependent_at_call_time());
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A return-only Self signature is receiver-dependent at call time") {
	SymbolicSelfLanguageScope language;

	// `return_type` is installed in the same post-`write_end()` block as the rest tail and feeds the
	// same cache, so it would go stale in exactly the same way.
	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Base:\n"
			"\tstatic func make() -> Self:\n"
			"\t\treturn Self.new()\n"
			"\n"
			"\tstatic func plain() -> long:\n"
			"\t\treturn 0\n");

	const FSFunction *make = symbolic_self_member_function(script, SNAME("Base"), SNAME("make"));
	REQUIRE(make != nullptr);
	CHECK(make->get_return_type().is_self_type);
	CHECK(make->is_receiver_dependent_at_call_time());

	const FSFunction *plain = symbolic_self_member_function(script, SNAME("Base"), SNAME("plain"));
	REQUIRE(plain != nullptr);
	CHECK_FALSE(plain->is_receiver_dependent_at_call_time());
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A witness class handle keeps the marker") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"trait Handing:\n"
			"\tabstract static func handle(value: Type[Self]) -> Type[Self]\n"
			"\n"
			"extend RefCounted uses Handing:\n"
			"\tstatic func handle(value: Type[Self]) -> Type[Self]:\n"
			"\t\treturn value\n");
	SymbolicSelfConformanceScope conformance_scope(script->get_script_path());

	const FSFunction *witness = FSConformanceRegistry::get_singleton()->find_native_witness_function(
			SNAME("RefCounted"), SNAME("handle"));
	REQUIRE(witness != nullptr);

	const FSDataType *parameter = symbolic_self_argument_type(witness, 0);
	REQUIRE(parameter != nullptr);
	CHECK(parameter->is_type_handle);
	CHECK(parameter->is_self_type);
	CHECK(witness->get_return_type().is_type_handle);
	CHECK(witness->get_return_type().is_self_type);
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A position written as the target is not marked") {
	SymbolicSelfLanguageScope language;

	// The resolver rebinds every marked position to the call-frame receiver, so marking a type the
	// author spelled out would silently re-specialize a deliberately fixed signature.
	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"trait Echoing:\n"
			"\tabstract static func echo(value: RefCounted) -> RefCounted\n"
			"\n"
			"extend RefCounted uses Echoing:\n"
			"\tstatic func echo(value: RefCounted) -> RefCounted:\n"
			"\t\treturn value\n");
	SymbolicSelfConformanceScope conformance_scope(script->get_script_path());

	const FSFunction *witness = FSConformanceRegistry::get_singleton()->find_native_witness_function(
			SNAME("RefCounted"), SNAME("echo"));
	REQUIRE(witness != nullptr);

	const FSDataType *parameter = symbolic_self_argument_type(witness, 0);
	REQUIRE(parameter != nullptr);
	CHECK(parameter->native_type == SNAME("RefCounted"));
	CHECK_FALSE(parameter->is_self_type);
	CHECK_FALSE(witness->get_return_type().is_self_type);
	CHECK_FALSE(witness->get_return_type().references_self_type());
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] An ordinary class type argument is not marked") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Crate[T]:\n"
			"\tvar value: T\n"
			"\n"
			"class Base:\n"
			"\tstatic func packed() -> Crate[Base]:\n"
			"\t\treturn Crate[Base].new()\n");

	const FSFunction *packed = symbolic_self_member_function(script, SNAME("Base"), SNAME("packed"));
	REQUIRE(packed != nullptr);
	CHECK_FALSE(packed->get_return_type().references_self_type());
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] A callable signature keeps Self in analysis") {
	SymbolicSelfLanguageScope language;

	// A lowered `Callable` carries no signature descriptor, so there is no runtime position to mark.
	// The analyzed signature is what a later slice reads to decide a callable is receiver-dependent,
	// so `Self` has to still be there rather than collapsed into the declaring class.
	const String source =
			"class Base:\n"
			"\tstatic func apply(action: Callable[[Self], Self]) -> void:\n"
			"\t\tpass\n";

	FSParser parser;
	REQUIRE(parser.parse(source, "user://test_symbolic_self_callable.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const FSParser::FunctionNode *apply = nullptr;
	for (int i = 0; i < root_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = root_class->members[i];
		if (member.type != FSParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
				member.m_class->identifier == nullptr || member.m_class->identifier->name != SNAME("Base")) {
			continue;
		}
		for (int j = 0; j < member.m_class->members.size(); j++) {
			const FSParser::ClassNode::Member &inner = member.m_class->members[j];
			if (inner.type == FSParser::ClassNode::Member::FUNCTION && inner.function != nullptr &&
					inner.function->identifier != nullptr && inner.function->identifier->name == SNAME("apply")) {
				apply = inner.function;
			}
		}
	}
	REQUIRE(apply != nullptr);
	REQUIRE_EQ(apply->parameters.size(), 1);

	const FSParser::DataType action_type = apply->parameters[0]->get_datatype();
	REQUIRE_EQ(action_type.method_parameter_types.size(), 1);
	REQUIRE_EQ(action_type.method_return_type.size(), 1);
	CHECK(action_type.method_parameter_types[0].kind == FSParser::DataType::TYPE_PARAMETER);
	CHECK(action_type.method_parameter_types[0].type_parameter_name == SNAME("@Self"));
	CHECK(action_type.method_return_type[0].kind == FSParser::DataType::TYPE_PARAMETER);
	CHECK(action_type.method_return_type[0].type_parameter_name == SNAME("@Self"));
}

TEST_CASE("[Modules][FoundryScript][SymbolicSelf] Declaration-time witness validation still rejects a bad witness") {
	SymbolicSelfLanguageScope language;

	const String source =
			"trait Echoing:\n"
			"\tabstract static func echo(value: Self) -> Self\n"
			"\n"
			"extend RefCounted uses Echoing:\n"
			"\tstatic func echo(value: Self) -> int:\n"
			"\t\treturn 0\n";

	FSParser parser;
	parser.parse(source, "user://test_symbolic_self_invalid_witness.fs", false);
	FSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	CHECK_FALSE(parser.get_errors().is_empty());
}

// The cases above pin the marker for static members and conformance witnesses. An inherited instance
// member has no compiled-in receiver either, so its signature and body positions keep the marker
// through analysis and lowering; the running frame re-binds every marked position to the instance
// the method was invoked on. Substitution never happens here. Resolving the marker against the
// call-frame receiver is a separate concern, covered by the runtime-resolution tests.
TEST_CASE("[Modules][FoundryScript][SymbolicSelf] An inherited instance member keeps the marker") {
	SymbolicSelfLanguageScope language;

	const Ref<FoundryScript> script = compile_symbolic_self_source(
			"class Base:\n"
			"\tfunc consume(value: Self) -> Self:\n"
			"\t\treturn value\n"
			"\n"
			"\tfunc elements() -> Array[Self]:\n"
			"\t\tvar out: Array[Self] = []\n"
			"\t\treturn out\n"
			"\n"
			"\tfunc mapping() -> Dictionary[String, Self]:\n"
			"\t\treturn {}\n"
			"\n"
			"class Derived extends Base:\n"
			"\t\tpass\n");

	const FSFunction *consume = symbolic_self_member_function(script, SNAME("Base"), SNAME("consume"));
	REQUIRE(consume != nullptr);
	const FSDataType *parameter = symbolic_self_argument_type(consume, 0);
	REQUIRE(parameter != nullptr);
	CHECK(parameter->is_self_type);
	CHECK(consume->get_return_type().is_self_type);
	CHECK(consume->has_self_referencing_signature());

	const FSFunction *elements = symbolic_self_member_function(script, SNAME("Base"), SNAME("elements"));
	REQUIRE(elements != nullptr);
	REQUIRE_EQ(elements->get_return_type().container_element_types.size(), 1);
	CHECK(elements->get_return_type().container_element_types[0].is_self_type);

	const FSFunction *mapping = symbolic_self_member_function(script, SNAME("Base"), SNAME("mapping"));
	REQUIRE(mapping != nullptr);
	REQUIRE_EQ(mapping->get_return_type().container_element_types.size(), 2);
	CHECK_FALSE(mapping->get_return_type().container_element_types[0].is_self_type);
	CHECK(mapping->get_return_type().container_element_types[1].is_self_type);
}

} // namespace FSTests
