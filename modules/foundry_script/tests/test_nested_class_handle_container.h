/**************************************************************************/
/*  test_nested_class_handle_container.h                                  */
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
#include "../fs_parser.h"

#include "core/error/error_macros.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"
#include "scene/main/node.h"

#include "tests/test_macros.h"

// Coverage for nested `Type[T]` in Array and Dictionary element slots. The `.fs` fixtures under
// `modules/foundry_script/tests/scripts` cover the source-level acceptances and rejections; these
// cases assert on the compiled runtime descriptor a script member actually carries, which is what
// makes the analyzer's promise enforceable outside analyzed source.

namespace FSTests {

struct ScopedNestedHandleLanguage {
	ScopedNestedHandleLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

// Captures every engine error raised during a write so a rejection can be asserted on by reason
// rather than by the mere fact that the write failed. A rejected container write raises the reason
// first and the failed-condition report second, so only the accumulated text carries the reason.
struct NestedHandleErrorRecorder {
	NestedHandleErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~NestedHandleErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		NestedHandleErrorRecorder *self = static_cast<NestedHandleErrorRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	void clear() { messages = String(); }

	ErrorHandlerList handler;
	String messages;
};

static Ref<FoundryScript> compile_nested_handle_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_nested_class_handle_container_%d.fs", unique_index++);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	FSParser parser;
	Error error = parser.parse(p_source, script->get_path(), false);
	REQUIRE(error == OK);

	FSAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	FSCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

TEST_CASE("[Modules][FoundryScript][NestedClassHandle] Container members compile to class-handle element descriptors") {
	ScopedNestedHandleLanguage language;

	Ref<FoundryScript> script = compile_nested_handle_source(
			"extends RefCounted\n"
			"\n"
			"var handles: Array[Type[Node]] = []\n"
			"var instances: Array[Node] = []\n"
			"var registry: Dictionary[String, Type[Node]] = {}\n"
			"var keyed: Dictionary[Type[Node], String] = {}\n"
			"var deep: Array[Dictionary[String, Type[Node]]] = []\n");

	Callable::CallError call_error;
	const Variant instance = script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance_object = instance;
	REQUIRE(instance_object != nullptr);

	const Array handles = instance_object->get(SNAME("handles"));
	CHECK(handles.get_element_type().is_type_handle);
	CHECK(handles.get_element_type().get_type_name() == "Type[Node]");

	// An instance-typed container in the same script is untouched, so the flag is not applied
	// indiscriminately to every object element.
	const Array instances = instance_object->get(SNAME("instances"));
	CHECK_FALSE(instances.get_element_type().is_type_handle);

	const Dictionary registry = instance_object->get(SNAME("registry"));
	CHECK_FALSE(registry.get_key_type().is_type_handle);
	CHECK(registry.get_value_type().is_type_handle);

	const Dictionary keyed = instance_object->get(SNAME("keyed"));
	CHECK(keyed.get_key_type().is_type_handle);
	CHECK_FALSE(keyed.get_value_type().is_type_handle);

	// Nesting deeper than one level keeps the flag on the leaf node only.
	const Array deep = instance_object->get(SNAME("deep"));
	const ContainerType deep_element = deep.get_element_type();
	CHECK_FALSE(deep_element.is_type_handle);
	REQUIRE(deep_element.element_types.size() == 2);
	CHECK_FALSE(deep_element.element_types[0].is_type_handle);
	CHECK(deep_element.element_types[1].is_type_handle);
	CHECK(deep_element.get_type_name() == "Dictionary[String, Type[Node]]");
}

TEST_CASE("[Modules][FoundryScript][NestedClassHandle] A nested handle element reports the actual rejection reason") {
	ScopedNestedHandleLanguage language;

	Ref<FoundryScript> script = compile_nested_handle_source(
			"extends RefCounted\n"
			"\n"
			"var handles: Array[Type[Node]] = []\n");

	Callable::CallError call_error;
	const Variant instance = script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance_object = instance;
	REQUIRE(instance_object != nullptr);

	Array handles = instance_object->get(SNAME("handles"));

	NestedHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	// A freed handle must be reported as freed, not as an inheritance failure of the handle object.
	Object *freed_object = memnew(Object);
	Variant freed_handle = freed_object;
	memdelete(freed_object);
	handles.push_back(freed_handle);
	CHECK(handles.is_empty());
	CHECK(recorder.messages.contains("previously freed"));
	CHECK_FALSE(recorder.messages.contains("does not inherit"));

	// An instance of the represented type is not a handle for it.
	recorder.clear();
	Node *node = memnew(Node);
	handles.push_back(Variant(node));
	CHECK(handles.is_empty());
	CHECK(recorder.messages.contains("requires a class handle"));
	CHECK_FALSE(recorder.messages.contains("does not inherit"));
	memdelete(node);

	ERR_PRINT_ON;
}

} // namespace FSTests
