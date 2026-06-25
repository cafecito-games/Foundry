/**************************************************************************/
/*  test_proxy.h                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "../gdscript.h"
#include "../gdscript_analyzer.h"
#include "../gdscript_compiler.h"
#include "../gdscript_parser.h"
#include "../gdscript_proxy.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// Initializes the GDScript language once for the suite. Mirrors the guard used
// by the analyzer tests: `init()` registers native globals (RefCounted, etc.)
// that compilation depends on.
struct ScopedProxyLanguage {
	ScopedProxyLanguage() {
		if (!GDScriptLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			GDScriptLanguage::get_singleton()->init();
		}
	}
};

static Ref<GDScript> get_subclass(const Ref<GDScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<GDScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<GDScript>();
}

static Ref<GDScript> compile_proxy_source(const String &p_source) {
	// A unique path per compile avoids "resource already loaded from path"
	// collisions when a single test keeps more than one script alive at once.
	static int unique_index = 0;
	const String path = vformat("user://test_proxy_%d.gd", unique_index++);

	Ref<GDScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	GDScriptParser parser;
	Error error = parser.parse(p_source, script->get_path(), false);
	REQUIRE(error == OK);

	GDScriptAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	GDScriptCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

// A trait with one required (abstract) and one concrete method, plus a GDScript
// recorder used as the proxy handler so the test can observe interception.
static const char *PROXY_FIXTURE_SOURCE =
		"trait Greeter:\n"
		"\t@abstract func greet(subject: String) -> String\n"
		"\tfunc describe() -> String:\n"
		"\t\treturn \"real-describe\"\n"
		"\n"
		"class Recorder:\n"
		"\tvar calls: Array = []\n"
		"\tvar last_args: Array = []\n"
		"\tfunc handle(method_name, args):\n"
		"\t\tcalls.append(str(method_name))\n"
		"\t\tlast_args = args\n"
		"\t\treturn \"stub:\" + str(method_name)\n";

TEST_CASE("[Modules][GDScript][Proxy] Contract methods route to the handler") {
	ScopedProxyLanguage language;

	Ref<GDScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<GDScript> greeter = get_subclass(script, "Greeter");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(greeter.is_valid());
	REQUIRE(greeter->is_trait_type());
	REQUIRE(recorder_script.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	REQUIRE(construct_error.error == Callable::CallError::CALL_OK);
	Object *recorder = recorder_ref;
	REQUIRE(recorder != nullptr);

	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());

	// A required (abstract) method reaches the handler and returns its stub.
	{
		Variant subject = "World";
		const Variant *args[1] = { &subject };
		Callable::CallError error;
		Variant result = proxy->callp("greet", args, 1, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant("stub:greet"));
	}

	// A concrete method is also intercepted; its real body never runs.
	{
		Callable::CallError error;
		Variant result = proxy->callp("describe", nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant("stub:describe"));
		CHECK(result != Variant("real-describe"));
	}

	// The handler observed both calls, in order, with the forwarded arguments.
	Variant calls = recorder->get("calls");
	REQUIRE(calls.get_type() == Variant::ARRAY);
	Array calls_array = calls;
	REQUIRE(calls_array.size() == 2);
	CHECK(calls_array[0] == Variant("greet"));
	CHECK(calls_array[1] == Variant("describe"));

	Array last_args = recorder->get("last_args");
	CHECK(last_args.is_empty()); // describe() had no arguments.
}

TEST_CASE("[Modules][GDScript][Proxy] Native built-ins are not intercepted") {
	ScopedProxyLanguage language;

	Ref<GDScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<GDScript> greeter = get_subclass(script, "Greeter");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
	REQUIRE(proxy.is_valid());

	// The script instance reports a non-contract method as "invalid" so
	// `Object::callp` falls through to the native implementation.
	ScriptInstance *instance = proxy->get_script_instance();
	REQUIRE(instance != nullptr);
	CHECK_FALSE(instance->has_method("get_instance_id"));
	{
		Callable::CallError error;
		instance->callp("get_instance_id", nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_ERROR_INVALID_METHOD);
	}

	// Going through the Object, the native call resolves and refcounting works.
	CHECK(ObjectDB::get_instance(proxy->get_instance_id()) == proxy.ptr());
	{
		Callable::CallError error;
		Variant id = proxy->callp("get_instance_id", nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(uint64_t(id.operator int64_t()) == uint64_t(proxy->get_instance_id()));
	}

	// The handler recorded nothing: no native call was intercepted.
	Array calls = recorder->get("calls");
	CHECK(calls.is_empty());
}

TEST_CASE("[Modules][GDScript][Proxy] get_script identity and method list") {
	ScopedProxyLanguage language;

	Ref<GDScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<GDScript> greeter = get_subclass(script, "Greeter");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
	REQUIRE(proxy.is_valid());

	CHECK(proxy->get_script_instance()->get_script() == greeter);

	List<MethodInfo> methods;
	proxy->get_script_instance()->get_method_list(&methods);
	bool has_greet = false;
	bool has_describe = false;
	for (const MethodInfo &method : methods) {
		if (method.name == StringName("greet")) {
			has_greet = true;
		} else if (method.name == StringName("describe")) {
			has_describe = true;
		}
	}
	CHECK(has_greet);
	CHECK(has_describe);
}

TEST_CASE("[Modules][GDScript][Proxy] Abstract types can be proxied") {
	ScopedProxyLanguage language;

	const char *source =
			"@abstract class Service:\n"
			"\t@abstract func compute(value: int) -> int\n"
			"\tfunc helper() -> String:\n"
			"\t\treturn \"real-helper\"\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn 42\n";

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> service = get_subclass(script, "Service");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(service.is_valid());
	REQUIRE(service->is_abstract());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(service, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());

	Variant value = 7;
	const Variant *args[1] = { &value };
	Callable::CallError error;
	Variant result = proxy->callp("compute", args, 1, error);
	CHECK(error.error == Callable::CallError::CALL_OK);
	CHECK(result == Variant(42));
}

TEST_CASE("[Modules][GDScript][Proxy] Invalid construction is rejected") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Greeter:\n"
			"\t@abstract func greet(subject: String) -> String\n"
			"\n"
			"class Concrete:\n"
			"\tfunc run() -> void:\n"
			"\t\tpass\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> greeter = get_subclass(script, "Greeter");
	Ref<GDScript> concrete = get_subclass(script, "Concrete");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(greeter.is_valid());
	REQUIRE(concrete.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	// A concrete class (no trait/abstract supertype) cannot be proxied.
	{
		String error_message;
		Ref<RefCounted> proxy = GDScriptProxy::create_proxy(concrete, Callable(recorder, "handle"), error_message);
		CHECK(proxy.is_null());
		CHECK_FALSE(error_message.is_empty());
	}

	// A null script is rejected.
	{
		String error_message;
		Ref<RefCounted> proxy = GDScriptProxy::create_proxy(Ref<Script>(), Callable(recorder, "handle"), error_message);
		CHECK(proxy.is_null());
		CHECK_FALSE(error_message.is_empty());
	}

	// An invalid (null) handler is rejected.
	{
		String error_message;
		Ref<RefCounted> proxy = GDScriptProxy::create_proxy(greeter, Callable(), error_message);
		CHECK(proxy.is_null());
		CHECK_FALSE(error_message.is_empty());
	}
}

TEST_CASE("[Modules][GDScript][Proxy] Lifetime is managed by refcounting") {
	ScopedProxyLanguage language;

	Ref<GDScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<GDScript> greeter = get_subclass(script, "Greeter");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	ObjectID proxy_id;
	{
		String error_message;
		Ref<RefCounted> proxy = GDScriptProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
		REQUIRE(proxy.is_valid());
		proxy_id = proxy->get_instance_id();
		CHECK(ObjectDB::get_instance(proxy_id) != nullptr);
	}
	// Dropping the last reference frees the proxy (and its ScriptInstance).
	CHECK(ObjectDB::get_instance(proxy_id) == nullptr);
}

TEST_CASE("[Modules][GDScript][Proxy] Non-RefCounted native bases are rejected") {
	ScopedProxyLanguage language;

	const char *source =
			"@abstract class NodeService extends Node:\n"
			"\t@abstract func ping() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> node_service = get_subclass(script, "NodeService");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(node_service.is_valid());
	REQUIRE(node_service->get_instance_base_type() == StringName("Node"));

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	// The host is a RefCounted, so a Node-rooted target cannot be proxied yet.
	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(node_service, Callable(recorder, "handle"), error_message);
	CHECK(proxy.is_null());
	CHECK_FALSE(error_message.is_empty());
}

} // namespace GDScriptTests
