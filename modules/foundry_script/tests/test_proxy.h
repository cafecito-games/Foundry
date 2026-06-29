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

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_compiler.h"
#include "../fs_parser.h"
#include "../fs_proxy.h"
#include "../fs_reflection.h"

#include "tests/test_macros.h"

namespace FSTests {

// Initializes the FoundryScript language once for the suite. Mirrors the guard used
// by the analyzer tests: `init()` registers native globals (RefCounted, etc.)
// that compilation depends on.
struct ScopedProxyLanguage {
	ScopedProxyLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

static Ref<FoundryScript> get_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

static Ref<FoundryScript> compile_proxy_source(const String &p_source) {
	// A unique path per compile avoids "resource already loaded from path"
	// collisions when a single test keeps more than one script alive at once.
	static int unique_index = 0;
	const String path = vformat("user://test_proxy_%d.fs", unique_index++);

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

// A trait with one required (abstract) and one concrete method, plus a FoundryScript
// recorder used as the proxy handler so the test can observe interception.
static const char *PROXY_FIXTURE_SOURCE =
		"trait Greeter:\n"
		"\tabstract func greet(subject: String) -> String\n"
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

TEST_CASE("[Modules][FoundryScript][Proxy] Contract methods route to the handler") {
	ScopedProxyLanguage language;

	Ref<FoundryScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<FoundryScript> greeter = get_subclass(script, "Greeter");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(greeter.is_valid());
	REQUIRE(greeter->is_trait_type());
	REQUIRE(recorder_script.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	REQUIRE(construct_error.error == Callable::CallError::CALL_OK);
	Object *recorder = recorder_ref;
	REQUIRE(recorder != nullptr);

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
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

TEST_CASE("[Modules][FoundryScript][Proxy] Native built-ins are not intercepted") {
	ScopedProxyLanguage language;

	Ref<FoundryScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<FoundryScript> greeter = get_subclass(script, "Greeter");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
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

TEST_CASE("[Modules][FoundryScript][Proxy] get_script identity and method list") {
	ScopedProxyLanguage language;

	Ref<FoundryScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<FoundryScript> greeter = get_subclass(script, "Greeter");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
	REQUIRE(proxy.is_valid());

	CHECK(proxy->get_script_instance()->get_script() == greeter);
	// Synthetic flag keeps language-keyed casts (e.g. inst_to_dict) from treating
	// the proxy as a FSInstance.
	CHECK(proxy->get_script_instance()->is_synthetic());

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

TEST_CASE("[Modules][FoundryScript][Proxy] Abstract types can be proxied") {
	ScopedProxyLanguage language;

	const char *source =
			"abstract class Service:\n"
			"\tabstract func compute(value: int) -> int\n"
			"\tfunc helper() -> String:\n"
			"\t\treturn \"real-helper\"\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn 42\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> service = get_subclass(script, "Service");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(service.is_valid());
	REQUIRE(service->is_abstract());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(service, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());

	Variant value = 7;
	const Variant *args[1] = { &value };
	Callable::CallError error;
	Variant result = proxy->callp("compute", args, 1, error);
	CHECK(error.error == Callable::CallError::CALL_OK);
	CHECK(result == Variant(42));
}

TEST_CASE("[Modules][FoundryScript][Proxy] Stringification routes _to_string through the handler") {
	ScopedProxyLanguage language;

	// `Object::to_string()` invokes the `ScriptInstance::to_string()` virtual rather
	// than `callp`, so a proxy whose contract declares `_to_string` must override that
	// virtual to reach the handler — otherwise implicit stringification silently skips
	// it. A trait that does not declare `_to_string` must not invoke the handler.
	const char *source =
			"abstract class Describable:\n"
			"\tabstract func _to_string() -> String\n"
			"\n"
			"trait Plain:\n"
			"\tabstract func work() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tvar calls: Array = []\n"
			"\tfunc handle(method_name, args):\n"
			"\t\tcalls.append(str(method_name))\n"
			"\t\treturn \"stubbed-string\"\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> describable = get_subclass(script, "Describable");
	Ref<FoundryScript> plain = get_subclass(script, "Plain");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(describable.is_valid());
	REQUIRE(plain.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	// Contract declares `_to_string`: implicit stringification reaches the handler and
	// returns its (coerced) value.
	{
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(describable, Callable(recorder, "handle"), error_message);
		REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());

		// `Object::to_string` dispatches through the script instance's `to_string`.
		CHECK(proxy->to_string() == String("stubbed-string"));

		// The same value is produced when a Variant stringifies the object implicitly.
		CHECK(String(Variant(proxy)) == String("stubbed-string"));

		Array calls = recorder->get("calls");
		REQUIRE(calls.size() >= 1);
		CHECK(calls.has("_to_string"));
	}

	// Contract does not declare `_to_string`: the handler is not invoked, and the
	// object falls back to the engine's default stringification.
	{
		recorder->set("calls", Array());
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(plain, Callable(recorder, "handle"), error_message);
		REQUIRE(proxy.is_valid());

		String result = proxy->to_string();
		Array calls = recorder->get("calls");
		CHECK(calls.is_empty());
		CHECK_FALSE(result == String("stubbed-string"));
	}
}

TEST_CASE("[Modules][FoundryScript][Proxy] Invalid construction is rejected") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Greeter:\n"
			"\tabstract func greet(subject: String) -> String\n"
			"\n"
			"class Concrete:\n"
			"\tfunc run() -> void:\n"
			"\t\tpass\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> greeter = get_subclass(script, "Greeter");
	Ref<FoundryScript> concrete = get_subclass(script, "Concrete");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(greeter.is_valid());
	REQUIRE(concrete.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	// A concrete class (no trait/abstract supertype) cannot be proxied.
	{
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(concrete, Callable(recorder, "handle"), error_message);
		CHECK(proxy.is_null());
		CHECK_FALSE(error_message.is_empty());
	}

	// A null script is rejected.
	{
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(Ref<Script>(), Callable(recorder, "handle"), error_message);
		CHECK(proxy.is_null());
		CHECK_FALSE(error_message.is_empty());
	}

	// An invalid (null) handler is rejected.
	{
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(greeter, Callable(), error_message);
		CHECK(proxy.is_null());
		CHECK_FALSE(error_message.is_empty());
	}
}

TEST_CASE("[Modules][FoundryScript][Proxy] Lifetime is managed by refcounting") {
	ScopedProxyLanguage language;

	Ref<FoundryScript> script = compile_proxy_source(PROXY_FIXTURE_SOURCE);
	Ref<FoundryScript> greeter = get_subclass(script, "Greeter");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	ObjectID proxy_id;
	{
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(greeter, Callable(recorder, "handle"), error_message);
		REQUIRE(proxy.is_valid());
		proxy_id = proxy->get_instance_id();
		CHECK(ObjectDB::get_instance(proxy_id) != nullptr);
	}
	// Dropping the last reference frees the proxy (and its ScriptInstance).
	CHECK(ObjectDB::get_instance(proxy_id) == nullptr);
}

TEST_CASE("[Modules][FoundryScript][Proxy] Non-RefCounted native bases are rejected") {
	ScopedProxyLanguage language;

	// `Object` and `Node` are not reference-counted, so a proxy — whose host is owned
	// through a `Ref` and freed by refcounting — cannot soundly represent them. Both
	// are rejected with a message; only RefCounted-derived bases are supported.
	const char *source =
			"abstract class NodeService extends Node:\n"
			"\tabstract func ping() -> void\n"
			"\n"
			"abstract class ObjectService extends Object:\n"
			"\tabstract func tick() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> node_service = get_subclass(script, "NodeService");
	Ref<FoundryScript> object_service = get_subclass(script, "ObjectService");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(node_service.is_valid());
	REQUIRE(node_service->get_instance_base_type() == StringName("Node"));
	REQUIRE(object_service.is_valid());
	REQUIRE(object_service->get_instance_base_type() == StringName("Object"));

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	for (const Ref<FoundryScript> &target : { node_service, object_service }) {
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(target, Callable(recorder, "handle"), error_message);
		CHECK(proxy.is_null());
		CHECK_FALSE(error_message.is_empty());
	}
}

TEST_CASE("[Modules][FoundryScript][Proxy] RefCounted-derived native bases are proxied") {
	ScopedProxyLanguage language;

	// `Resource` derives from `RefCounted`, so a proxy of a `Resource`-rooted type is
	// hosted on a real `Resource`: it satisfies `is Resource`, its native methods and
	// properties resolve, and its contract still routes through the handler.
	const char *source =
			"abstract class ResourceService extends Resource:\n"
			"\tvar slot: int\n"
			"\tabstract func compute() -> int\n"
			"\n"
			"class Recorder:\n"
			"\tvar calls: Array = []\n"
			"\tfunc handle(method_name, args):\n"
			"\t\tcalls.append(str(method_name))\n"
			"\t\treturn 42\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> service = get_subclass(script, "ResourceService");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(service.is_valid());
	REQUIRE(service->get_instance_base_type() == StringName("Resource"));

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(service, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());

	// The host is a genuine Resource, not a bare RefCounted.
	CHECK(proxy->get_class() == String("Resource"));
	Resource *as_resource = Object::cast_to<Resource>(proxy.ptr());
	CHECK(as_resource != nullptr);

	// A native Resource property resolves through native dispatch (the proxy does not
	// intercept it: `resource_name` is not part of `T`'s declared contract).
	proxy->set("resource_name", "my-resource");
	CHECK(proxy->get("resource_name") == Variant("my-resource"));

	// The abstract contract method still routes to the handler.
	{
		Callable::CallError error;
		Variant result = proxy->get_script_instance()->callp("compute", nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant(42));
	}

	// The declared `var slot` is still backed by the auto-backing store.
	{
		Variant value;
		CHECK(proxy->get_script_instance()->set("slot", 5));
		CHECK(proxy->get_script_instance()->get("slot", value));
		CHECK(value == Variant(5));
	}
}

TEST_CASE("[Modules][FoundryScript][Proxy] Property writes validate against declared types") {
	ScopedProxyLanguage language;

	// A proxy's auto-backing store must validate/coerce writes exactly as a real
	// FSInstance does, including typed containers. `RealBag` mirrors `Bag`'s
	// declared vars, so the proxy of `Bag` is compared against a concrete instance for
	// each write: both must agree on the accepted/rejected result and the stored value.
	const char *source =
			"trait Bag:\n"
			"\tvar count: int\n"
			"\tvar ratio: float\n"
			"\tvar label: String\n"
			"\tvar tags: Array[int]\n"
			"\tvar scores: Dictionary[String, int]\n"
			"\t@export var flagged = 7\n"
			"\tabstract func use() -> void\n"
			"\n"
			"class RealBag:\n"
			"\tvar count: int\n"
			"\tvar ratio: float\n"
			"\tvar label: String\n"
			"\tvar tags: Array[int]\n"
			"\tvar scores: Dictionary[String, int]\n"
			"\t@export var flagged = 7\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> bag = get_subclass(script, "Bag");
	Ref<FoundryScript> real_bag_script = get_subclass(script, "RealBag");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(bag.is_valid());
	REQUIRE(real_bag_script.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	Variant real_bag_ref = real_bag_script->_new(nullptr, -1, construct_error);
	REQUIRE(construct_error.error == Callable::CallError::CALL_OK);
	Object *real_bag = real_bag_ref;
	ScriptInstance *real_instance = real_bag->get_script_instance();
	REQUIRE(real_instance != nullptr);

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(bag, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());
	ScriptInstance *proxy_instance = proxy->get_script_instance();

	Array typed_tags;
	typed_tags.set_typed(Variant::INT, StringName(), Variant());
	typed_tags.push_back(1);
	typed_tags.push_back(2);

	Array wrong_tags;
	wrong_tags.set_typed(Variant::STRING, StringName(), Variant());
	wrong_tags.push_back("x");

	Dictionary typed_scores;
	typed_scores.set_typed(Variant::STRING, StringName(), Variant(), Variant::INT, StringName(), Variant());
	typed_scores["a"] = 1;

	Dictionary untyped_scores;
	untyped_scores["a"] = 1;

	struct Write {
		const char *name;
		Variant value;
	};
	const Write writes[] = {
		{ "count", 5 }, // exact int.
		{ "count", 5.0 }, // float -> int coercion.
		{ "count", "oops" }, // non-numeric string.
		{ "ratio", 3 }, // int -> float coercion.
		{ "label", 42 }, // int -> String.
		{ "tags", typed_tags }, // correctly-typed Array[int].
		{ "tags", Array() }, // untyped Array.
		{ "tags", wrong_tags }, // wrong-element Array[String].
		{ "scores", typed_scores }, // correctly-typed Dictionary.
		{ "scores", untyped_scores }, // untyped Dictionary.
	};

	for (const Write &write : writes) {
		const StringName name = write.name;

		const bool proxy_set = proxy_instance->set(name, write.value);
		const bool real_set = real_instance->set(name, write.value);
		CHECK_MESSAGE(proxy_set == real_set, vformat("set(\"%s\") accept/reject mismatch", String(name)).utf8().get_data());

		Variant proxy_value;
		Variant real_value;
		proxy_instance->get(name, proxy_value);
		real_instance->get(name, real_value);
		// Both must hold the same value after the write (rejected writes leave both
		// slots unchanged; accepted writes coerce to the same stored value).
		CHECK_MESSAGE(proxy_value == real_value, vformat("get(\"%s\") value mismatch after write", String(name)).utf8().get_data());
	}

	// The reproduction from the issue: a wrong primitive write is no longer silently
	// accepted-and-stored as the foreign value.
	{
		ERR_PRINT_OFF;
		proxy_instance->set("count", "oops");
		ERR_PRINT_ON;
		Variant value;
		proxy_instance->get("count", value);
		CHECK(value.get_type() == Variant::INT);
		CHECK(value != Variant("oops"));
	}

	// get_property_type still reports the export-inferred type for an otherwise-untyped
	// member (the validation type is Variant, but the reflected type is INT), matching
	// a real FSInstance.
	{
		bool proxy_valid = false;
		bool real_valid = false;
		const Variant::Type proxy_type = proxy_instance->get_property_type("flagged", &proxy_valid);
		const Variant::Type real_type = real_instance->get_property_type("flagged", &real_valid);
		CHECK(proxy_valid);
		CHECK(proxy_valid == real_valid);
		CHECK(proxy_type == real_type);
		CHECK(proxy_type == Variant::INT);
	}
}

TEST_CASE("[Modules][FoundryScript][Proxy] Auto-backing property store") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Bag:\n"
			"\tvar label: String\n"
			"\tvar count: int\n"
			"\tvar ratio: float\n"
			"\tvar items: Array\n"
			"\tvar tags: Array[int]\n"
			"\tvar scores: Dictionary[String, int]\n"
			"\tabstract func use() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tvar calls: Array = []\n"
			"\tfunc handle(method_name, args):\n"
			"\t\tcalls.append(str(method_name))\n"
			"\t\treturn null\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> bag = get_subclass(script, "Bag");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(bag.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(bag, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());
	ScriptInstance *instance = proxy->get_script_instance();

	// Declared vars default to the zero value of their declared type.
	{
		Variant value;
		CHECK(instance->get("label", value));
		CHECK(value == Variant(String()));
		CHECK(instance->get("count", value));
		CHECK(value == Variant(0));
		CHECK(instance->get("ratio", value));
		CHECK(value == Variant(0.0));
		CHECK(instance->get("items", value));
		CHECK(value.get_type() == Variant::ARRAY);

		// Typed containers default to a correctly-typed empty value.
		CHECK(instance->get("tags", value));
		REQUIRE(value.get_type() == Variant::ARRAY);
		Array tags = value;
		CHECK(tags.is_typed());
		CHECK(tags.get_typed_builtin() == Variant::INT);

		CHECK(instance->get("scores", value));
		REQUIRE(value.get_type() == Variant::DICTIONARY);
		Dictionary scores = value;
		CHECK(scores.is_typed_key());
		CHECK(scores.get_typed_key_builtin() == Variant::STRING);
		CHECK(scores.get_typed_value_builtin() == Variant::INT);
	}

	// set()/get() round-trip through the backing store.
	{
		Variant value;
		CHECK(instance->set("count", 7));
		CHECK(instance->get("count", value));
		CHECK(value == Variant(7));
		CHECK(instance->set("label", "hi"));
		CHECK(instance->get("label", value));
		CHECK(value == Variant("hi"));
	}

	// Names that are not declared vars are not handled by the store.
	{
		Variant value;
		CHECK_FALSE(instance->set("missing", 1));
		CHECK_FALSE(instance->get("missing", value));
	}

	// Property access never reaches the handler.
	{
		Array calls = recorder->get("calls");
		CHECK(calls.is_empty());
	}

	// get_property_type reports declared types.
	{
		bool is_valid = false;
		CHECK(instance->get_property_type("count", &is_valid) == Variant::INT);
		CHECK(is_valid);
		CHECK(instance->get_property_type("label", &is_valid) == Variant::STRING);
		CHECK(is_valid);
		instance->get_property_type("missing", &is_valid);
		CHECK_FALSE(is_valid);
	}

	// get_property_list exposes the declared vars (no user-written accessors).
	{
		List<PropertyInfo> properties;
		instance->get_property_list(&properties);
		HashSet<StringName> script_variables;
		for (const PropertyInfo &property : properties) {
			if (property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) {
				script_variables.insert(property.name);
			}
		}
		CHECK(script_variables.has("label"));
		CHECK(script_variables.has("count"));
		CHECK(script_variables.has("ratio"));
		CHECK(script_variables.has("items"));
	}
}

TEST_CASE("[Modules][FoundryScript][Proxy] is / trait conformance") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Drawable:\n"
			"\tabstract func draw_self() -> void\n"
			"\n"
			"trait Sprite uses Drawable:\n"
			"\tabstract func render() -> void\n"
			"\n"
			"trait Unrelated:\n"
			"\tabstract func z() -> void\n"
			"\n"
			"abstract class Base:\n"
			"\tabstract func a() -> void\n"
			"\n"
			"abstract class Derived extends Base:\n"
			"\tabstract func b() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> sprite = get_subclass(script, "Sprite");
	Ref<FoundryScript> drawable = get_subclass(script, "Drawable");
	Ref<FoundryScript> unrelated = get_subclass(script, "Unrelated");
	Ref<FoundryScript> base = get_subclass(script, "Base");
	Ref<FoundryScript> derived = get_subclass(script, "Derived");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(sprite.is_valid());
	REQUIRE(sprite->is_trait_type());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	// A trait conforms to its own identity, to traits it uses, but not to others.
	CHECK(sprite->has_script_trait(sprite->get_trait_type_name()));
	CHECK(sprite->has_script_trait(drawable->get_trait_type_name()));
	CHECK_FALSE(sprite->has_script_trait(unrelated->get_trait_type_name()));

	// A trait proxy reports the trait as its script, satisfying the runtime `is`
	// type test (which reads get_script() then walks the script/trait chain).
	String error_message;
	Ref<RefCounted> sprite_proxy = FSProxy::create_proxy(sprite, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(sprite_proxy.is_valid(), error_message.utf8().get_data());
	CHECK(sprite_proxy->get_script_instance()->get_script() == sprite);

	// An abstract-class proxy's script chain contains the class and its bases.
	Ref<RefCounted> derived_proxy = FSProxy::create_proxy(derived, Callable(recorder, "handle"), error_message);
	REQUIRE(derived_proxy.is_valid());
	Script *script_ptr = derived_proxy->get_script_instance()->get_script().ptr();
	bool reaches_base = false;
	while (script_ptr) {
		if (script_ptr == base.ptr()) {
			reaches_base = true;
			break;
		}
		script_ptr = script_ptr->get_base_script().ptr();
	}
	CHECK(reaches_base);
}

TEST_CASE("[Modules][FoundryScript][Proxy] Transitive abstract trait requirements are intercepted") {
	ScopedProxyLanguage language;

	// `Child` flattens nothing abstract from the traits it (transitively) uses, and
	// at runtime only retains trait identity names. A proxy of `Child` must still
	// intercept the abstract requirements contributed by `Middle` and `Base`, not
	// just `Child`'s own `child_required`. A diamond (`Wide uses Left, Right`, both
	// of which `use Base`) must reach the shared requirement exactly once.
	const char *source =
			"trait Base:\n"
			"\tabstract func base_required() -> int\n"
			"\tabstract func base_defaulted(scale: int = 3) -> int\n"
			"\tabstract func base_variadic(first: int, ...rest) -> int\n"
			"\n"
			"trait Middle uses Base:\n"
			"\tabstract func middle_required() -> int\n"
			"\n"
			"trait Child uses Middle:\n"
			"\tabstract func child_required() -> int\n"
			"\n"
			"trait Left uses Base:\n"
			"\tabstract func left_required() -> int\n"
			"\n"
			"trait Right uses Base:\n"
			"\tabstract func right_required() -> int\n"
			"\n"
			"trait Wide uses Left, Right:\n"
			"\tabstract func wide_required() -> int\n"
			"\n"
			"class Recorder:\n"
			"\tvar calls: Array = []\n"
			"\tfunc handle(method_name, args):\n"
			"\t\tcalls.append(str(method_name))\n"
			"\t\treturn 7\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> child = get_subclass(script, "Child");
	Ref<FoundryScript> wide = get_subclass(script, "Wide");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(child.is_valid());
	REQUIRE(child->is_trait_type());
	REQUIRE(wide.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	REQUIRE(construct_error.error == Callable::CallError::CALL_OK);
	Object *recorder = recorder_ref;

	const auto check_intercepts = [&](const Ref<FoundryScript> &p_type, const Vector<String> &p_methods) {
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(p_type, Callable(recorder, "handle"), error_message);
		REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());
		ScriptInstance *instance = proxy->get_script_instance();

		List<MethodInfo> methods;
		instance->get_method_list(&methods);
		HashSet<StringName> listed;
		for (const MethodInfo &method : methods) {
			listed.insert(method.name);
		}

		for (const String &method : p_methods) {
			const StringName method_name = method;
			CHECK(instance->has_method(method_name));
			// Enumeration is consistent with dispatch: every contract method is listed.
			CHECK_MESSAGE(listed.has(method_name), method.utf8().get_data());
			Callable::CallError error;
			Variant result = instance->callp(method_name, nullptr, 0, error);
			CHECK_MESSAGE(error.error == Callable::CallError::CALL_OK, method.utf8().get_data());
			CHECK(result == Variant(7));
		}
	};

	// Chain: a proxy of `Child` intercepts requirements transitively from `Middle`
	// and `Base`, with `int` returns coerced through the handler. `base_defaulted`
	// is callable with no arguments because its sole parameter has a default.
	check_intercepts(child, { "child_required", "middle_required", "base_required", "base_defaulted" });

	// Enumerated metadata for inherited requirements matches the compiled-function
	// convention: real defaults are preserved and `...rest` sets the vararg flag.
	{
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(child, Callable(recorder, "handle"), error_message);
		REQUIRE(proxy.is_valid());
		List<MethodInfo> methods;
		proxy->get_script_instance()->get_method_list(&methods);

		bool checked_defaulted = false;
		bool checked_variadic = false;
		for (const MethodInfo &method : methods) {
			if (method.name == StringName("base_defaulted")) {
				checked_defaulted = true;
				REQUIRE(method.arguments.size() == 1);
				REQUIRE(method.default_arguments.size() == 1);
				CHECK(method.default_arguments[0] == Variant(3));
				CHECK((method.flags & METHOD_FLAG_VARARG) == 0);
			} else if (method.name == StringName("base_variadic")) {
				checked_variadic = true;
				// The fixed parameter is listed; the `...rest` is conveyed by the flag.
				CHECK(method.arguments.size() == 1);
				CHECK((method.flags & METHOD_FLAG_VARARG) != 0);
			}
		}
		CHECK(checked_defaulted);
		CHECK(checked_variadic);

		// Argument-count metadata is reported for inherited requirements too, matching
		// their fixed-parameter count (the `...rest` is not counted).
		ScriptInstance *instance = proxy->get_script_instance();
		bool is_valid = false;
		CHECK(instance->get_method_argument_count("base_required", &is_valid) == 0);
		CHECK(is_valid);
		CHECK(instance->get_method_argument_count("base_defaulted", &is_valid) == 1);
		CHECK(is_valid);
		CHECK(instance->get_method_argument_count("base_variadic", &is_valid) == 1);
		CHECK(is_valid);
		instance->get_method_argument_count("not_a_method", &is_valid);
		CHECK_FALSE(is_valid);

		// A pre-populated list (as `Object::get_method_list` hands over, holding the
		// host's native methods) must not cause an identically-named contract method to
		// be dropped: the requirement is still appended.
		List<MethodInfo> seeded;
		MethodInfo native_lookalike;
		native_lookalike.name = "base_required";
		seeded.push_back(native_lookalike);
		instance->get_method_list(&seeded);
		int base_required_entries = 0;
		for (const MethodInfo &method : seeded) {
			if (method.name == StringName("base_required")) {
				base_required_entries++;
			}
		}
		CHECK(base_required_entries == 2); // the seeded look-alike plus the contract entry.
	}

	// Diamond: `base_required` is reached through both `Left` and `Right` and is
	// intercepted exactly once (no double-dispatch, no fallthrough).
	check_intercepts(wide, { "wide_required", "left_required", "right_required", "base_required" });
}

TEST_CASE("[Modules][FoundryScript][Proxy] Inherited requirement return matches a direct proxy") {
	ScopedProxyLanguage language;

	// An unannotated abstract method (`ping()`) compiles to a `void` return (no
	// declared type and no body that returns), so the handler's value is ignored —
	// not passed through as `Variant`. A proxy that reaches `ping` transitively
	// (through `Sub uses Pinger`) must coerce its return exactly as a direct proxy of
	// `Pinger` does, rather than upgrading it to a pass-through `Variant`. A
	// typed-but-void method (`reset() -> void`) is covered too.
	const char *source =
			"trait Pinger:\n"
			"\tabstract func ping()\n"
			"\tabstract func reset() -> void\n"
			"\n"
			"trait Sub uses Pinger:\n"
			"\tabstract func extra() -> int\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn \"value\"\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> pinger = get_subclass(script, "Pinger");
	Ref<FoundryScript> sub = get_subclass(script, "Sub");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(pinger.is_valid());
	REQUIRE(sub.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	const auto call_method = [&](const Ref<FoundryScript> &p_type, const char *p_method) {
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(p_type, Callable(recorder, "handle"), error_message);
		REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());
		Callable::CallError error;
		Variant result = proxy->get_script_instance()->callp(p_method, nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		return result;
	};

	// `ping` (unannotated -> void): the direct proxy ignores the handler value and
	// returns null, and the transitive proxy must do the same.
	CHECK(call_method(pinger, "ping").get_type() == Variant::NIL);
	CHECK(call_method(sub, "ping") == call_method(pinger, "ping"));

	// `reset` (void): the handler value is ignored on both paths.
	CHECK(call_method(pinger, "reset").get_type() == Variant::NIL);
	CHECK(call_method(sub, "reset").get_type() == Variant::NIL);
}

TEST_CASE("[Modules][FoundryScript][Proxy] A same-named property does not hide an inherited requirement") {
	ScopedProxyLanguage language;

	// `Combined` flattens a `tag` var from one trait and inherits an abstract `tag()`
	// method requirement from another. The property and the method occupy different
	// namespaces on the proxy, so both must survive: the var feeds the auto-backing
	// store, and the method is intercepted — the var must not suppress recording the
	// requirement.
	const char *source =
			"trait WithField:\n"
			"\tvar tag: int\n"
			"\n"
			"trait WithMethod:\n"
			"\tabstract func tag() -> int\n"
			"\n"
			"trait Combined uses WithField, WithMethod:\n"
			"\tpass\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn 42\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> combined = get_subclass(script, "Combined");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(combined.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(combined, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());
	ScriptInstance *instance = proxy->get_script_instance();

	// The inherited `tag()` method is part of the contract and reaches the handler.
	CHECK(instance->has_method("tag"));
	{
		Callable::CallError error;
		Variant result = instance->callp("tag", nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant(42));
	}

	// The `tag` var still round-trips through the auto-backing property store,
	// independently of the method, and never reaches the handler.
	{
		Variant value;
		CHECK(instance->set("tag", 7));
		CHECK(instance->get("tag", value));
		CHECK(value == Variant(7));
	}

	// A concrete trait method dropped by flattening (its name claimed by the earlier
	// `var tag`) is not a callable either, so it must not suppress the abstract `tag()`
	// requirement contributed by a third trait. The requirement is still intercepted.
	const char *shadowed_source =
			"trait WithField:\n"
			"\tvar tag: int\n"
			"\n"
			"trait WithConcrete:\n"
			"\tfunc tag() -> int:\n"
			"\t\treturn 1\n"
			"\n"
			"trait WithAbstract:\n"
			"\tabstract func tag() -> int\n"
			"\n"
			"trait Mixed uses WithField, WithConcrete, WithAbstract:\n"
			"\tpass\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn 42\n";

	Ref<FoundryScript> shadowed_script = compile_proxy_source(shadowed_source);
	Ref<FoundryScript> mixed = get_subclass(shadowed_script, "Mixed");
	Ref<FoundryScript> shadowed_recorder = get_subclass(shadowed_script, "Recorder");
	REQUIRE(mixed.is_valid());

	Variant shadowed_recorder_ref = shadowed_recorder->_new(nullptr, -1, construct_error);
	Object *shadowed_recorder_object = shadowed_recorder_ref;

	String shadowed_error;
	Ref<RefCounted> mixed_proxy = FSProxy::create_proxy(mixed, Callable(shadowed_recorder_object, "handle"), shadowed_error);
	REQUIRE_MESSAGE(mixed_proxy.is_valid(), shadowed_error.utf8().get_data());
	ScriptInstance *mixed_instance = mixed_proxy->get_script_instance();

	CHECK(mixed_instance->has_method("tag"));
	{
		Callable::CallError error;
		Variant result = mixed_instance->callp("tag", nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant(42));
	}
}

TEST_CASE("[Modules][FoundryScript][Proxy] Handler return coercion and validation") {
	ScopedProxyLanguage language;

	const char *source =
			"abstract class Service:\n"
			"\tabstract func get_count() -> int\n"
			"\tabstract func get_ratio() -> float\n"
			"\tabstract func get_label() -> String\n"
			"\tabstract func do_nothing() -> void\n"
			"\tabstract func get_anything() -> Variant\n"
			"\tabstract func get_tags() -> Array[int]\n"
			"\tabstract func get_scores() -> Dictionary[String, int]\n"
			"\n"
			"class Recorder:\n"
			"\tvar stub_return: Variant = null\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn stub_return\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> service = get_subclass(script, "Service");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(service.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(service, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());
	ScriptInstance *instance = proxy->get_script_instance();

	const auto call = [&](const char *p_method) {
		Callable::CallError error;
		Variant result = instance->callp(p_method, nullptr, 0, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		return result;
	};

	// Exact-typed return passes through.
	recorder->set("stub_return", 7);
	CHECK(call("get_count") == Variant(7));

	// int -> float implicit coercion.
	recorder->set("stub_return", 3);
	Variant ratio = call("get_ratio");
	CHECK(ratio.get_type() == Variant::FLOAT);
	CHECK(ratio == Variant(3.0));

	// String return.
	recorder->set("stub_return", "hi");
	CHECK(call("get_label") == Variant("hi"));

	// void method ignores whatever the handler returns.
	recorder->set("stub_return", 999);
	CHECK(call("do_nothing").get_type() == Variant::NIL);

	// Untyped (Variant) return passes any value through unchanged.
	recorder->set("stub_return", Vector2(1, 2));
	CHECK(call("get_anything") == Variant(Vector2(1, 2)));

	// Hard mismatch: a non-convertible value is reported (suppressed here) and
	// replaced with the declared type's default.
	recorder->set("stub_return", "not a number");
	{
		ERR_PRINT_OFF;
		Variant coerced = call("get_count");
		ERR_PRINT_ON;
		CHECK(coerced.get_type() == Variant::INT);
		CHECK(coerced == Variant(0));
	}

	// A correctly-typed container passes through.
	{
		Array typed_tags;
		typed_tags.set_typed(Variant::INT, StringName(), Variant());
		typed_tags.push_back(1);
		typed_tags.push_back(2);
		recorder->set("stub_return", typed_tags);
		Variant result = call("get_tags");
		REQUIRE(result.get_type() == Variant::ARRAY);
		Array result_tags = result;
		CHECK(result_tags.is_typed());
		CHECK(result_tags.size() == 2);
	}

	// An untyped container for a typed-container return is a mismatch (the VM
	// requires an exactly-typed Array on return), so it falls back to the typed
	// empty default rather than silently passing an untyped array.
	{
		Array untyped_tags;
		untyped_tags.push_back(1);
		recorder->set("stub_return", untyped_tags);
		ERR_PRINT_OFF;
		Variant result = call("get_tags");
		ERR_PRINT_ON;
		REQUIRE(result.get_type() == Variant::ARRAY);
		Array result_tags = result;
		CHECK(result_tags.is_typed());
		CHECK(result_tags.is_empty());
	}

	// A wrong-element-typed container (Array[String] for Array[int]) is also a
	// mismatch, not silently accepted via builtin conversion.
	{
		Array wrong_tags;
		wrong_tags.set_typed(Variant::STRING, StringName(), Variant());
		wrong_tags.push_back("x");
		recorder->set("stub_return", wrong_tags);
		ERR_PRINT_OFF;
		Variant result = call("get_tags");
		ERR_PRINT_ON;
		REQUIRE(result.get_type() == Variant::ARRAY);
		Array result_tags = result;
		CHECK(result_tags.is_typed());
		CHECK(result_tags.get_typed_builtin() == Variant::INT);
		CHECK(result_tags.is_empty());
	}

	// A correctly-typed Dictionary passes through; a wrong-typed one defaults.
	{
		Dictionary typed_scores;
		typed_scores.set_typed(Variant::STRING, StringName(), Variant(), Variant::INT, StringName(), Variant());
		typed_scores["a"] = 1;
		recorder->set("stub_return", typed_scores);
		Variant result = call("get_scores");
		REQUIRE(result.get_type() == Variant::DICTIONARY);
		Dictionary result_scores = result;
		CHECK(result_scores.is_typed_key());
		CHECK(result_scores.size() == 1);

		Dictionary untyped_scores;
		untyped_scores["a"] = 1;
		recorder->set("stub_return", untyped_scores);
		ERR_PRINT_OFF;
		Variant defaulted = call("get_scores");
		ERR_PRINT_ON;
		REQUIRE(defaulted.get_type() == Variant::DICTIONARY);
		Dictionary defaulted_scores = defaulted;
		CHECK(defaulted_scores.is_typed_key());
		CHECK(defaulted_scores.is_empty());
	}

	// If the handler becomes uninvocable (its target is freed), a contract call
	// must not fall through to native dispatch. It reports the failure and returns
	// the declared type's default with CALL_OK (mirroring the VM's handling of a
	// runtime error in a function body), never a CALL_ERROR_INVALID_METHOD /
	// CALL_ERROR_INSTANCE_IS_NULL that Object::callp would treat as "try native".
	{
		Callable::CallError throwaway_error;
		Variant throwaway_recorder = recorder_script->_new(nullptr, -1, throwaway_error);
		Object *throwaway = throwaway_recorder;

		String message;
		Ref<RefCounted> broken_proxy = FSProxy::create_proxy(service, Callable(throwaway, "handle"), message);
		REQUIRE(broken_proxy.is_valid());

		// Drop the only reference to the handler's target; the Callable holds it
		// weakly, so the next call cannot reach the handler.
		throwaway_recorder = Variant();

		Callable::CallError error;
		ERR_PRINT_OFF;
		Variant result = broken_proxy->get_script_instance()->callp("get_count", nullptr, 0, error);
		ERR_PRINT_ON;
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result.get_type() == Variant::INT);
		CHECK(result == Variant(0));
	}
}

static HashSet<String> descriptor_names(const TypedArray<Dictionary> &p_descriptors) {
	HashSet<String> names;
	for (int i = 0; i < p_descriptors.size(); i++) {
		const Dictionary descriptor = p_descriptors[i];
		names.insert(descriptor.get("name", ""));
	}
	return names;
}

TEST_CASE("[Modules][FoundryScript][Reflection] Read-only introspection API") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Drawable:\n"
			"\tabstract func draw_self() -> void\n"
			"\n"
			"trait Sprite uses Drawable:\n"
			"\tvar label: String\n"
			"\tvar count: int\n"
			"\tabstract func render() -> void\n"
			"\tfunc describe() -> String:\n"
			"\t\treturn \"sprite\"\n"
			"\n"
			"trait Unrelated:\n"
			"\tabstract func z() -> void\n"
			"\n"
			"abstract class Base:\n"
			"\tfunc base_method() -> int:\n"
			"\t\treturn 1\n"
			"\n"
			"abstract class Derived extends Base:\n"
			"\tabstract func own_method() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> sprite = get_subclass(script, "Sprite");
	Ref<FoundryScript> drawable = get_subclass(script, "Drawable");
	Ref<FoundryScript> unrelated = get_subclass(script, "Unrelated");
	Ref<FoundryScript> derived = get_subclass(script, "Derived");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(sprite.is_valid());

	FSReflection *reflection = memnew(FSReflection);

	// get_methods returns the script's declared methods as descriptor dicts.
	{
		HashSet<String> names = descriptor_names(reflection->get_methods(sprite));
		CHECK(names.has("render"));
		CHECK(names.has("describe"));
	}

	// get_method_info returns a single descriptor (empty for unknown methods).
	{
		Dictionary render_info = reflection->get_method_info(sprite, "render");
		CHECK(render_info.get("name", "") == Variant("render"));
		CHECK(reflection->get_method_info(sprite, "does_not_exist").is_empty());

		// Inherited methods resolve through the base chain (like get_methods).
		REQUIRE(derived.is_valid());
		Dictionary base_info = reflection->get_method_info(derived, "base_method");
		CHECK(base_info.get("name", "") == Variant("base_method"));
	}

	// get_properties returns the declared vars (no category headers).
	{
		HashSet<String> names = descriptor_names(reflection->get_properties(sprite));
		CHECK(names.has("label"));
		CHECK(names.has("count"));
	}

	// implements_trait accepts a trait type (robust) or its identity name.
	{
		CHECK(reflection->implements_trait(sprite, drawable)); // used trait
		CHECK(reflection->implements_trait(sprite, sprite)); // self-conformance via type
		CHECK_FALSE(reflection->implements_trait(sprite, unrelated));
		CHECK_FALSE(reflection->implements_trait(sprite, "NotATrait"));
	}

	// Works on an instance too: the instance's script is introspected.
	{
		Callable::CallError construct_error;
		Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
		Object *recorder = recorder_ref;
		String error_message;
		Ref<RefCounted> proxy = FSProxy::create_proxy(sprite, Callable(recorder, "handle"), error_message);
		REQUIRE(proxy.is_valid());
		HashSet<String> names = descriptor_names(reflection->get_methods(proxy));
		CHECK(names.has("render"));
		CHECK(reflection->implements_trait(proxy, drawable));
	}

	// Non-script targets yield empty results, not crashes.
	{
		CHECK(reflection->get_methods(Variant()).is_empty());
		CHECK(reflection->get_properties(42).is_empty());
		CHECK_FALSE(reflection->implements_trait(Variant(), drawable));
	}

	// A freed object is handled via the validated object, not dereferenced.
	{
		Object *freed = memnew(Object);
		Variant freed_target = freed;
		memdelete(freed);
		CHECK(reflection->get_methods(freed_target).is_empty());
		CHECK(reflection->get_properties(freed_target).is_empty());
		CHECK(reflection->get_method_info(freed_target, "x").is_empty());
		CHECK_FALSE(reflection->implements_trait(freed_target, drawable));
	}

	memdelete(reflection);
}

TEST_CASE("[Modules][FoundryScript][Reflection] create_proxy_dynamic builds a handler proxy") {
	ScopedProxyLanguage language;

	// `foundry.reflection.create_proxy_dynamic(type, handler)` is the namespaced surface
	// for building a dynamic proxy; it mirrors the bare `create_proxy_dynamic` utility
	// (which remains the `create_proxy[T]` codegen lowering target).
	const char *source =
			"trait Greeter:\n"
			"\tabstract func greet(subject: String) -> String\n"
			"\n"
			"class Recorder:\n"
			"\tvar calls: Array = []\n"
			"\tfunc handle(method_name, args):\n"
			"\t\tcalls.append(str(method_name))\n"
			"\t\treturn \"stub:\" + str(method_name)\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> greeter = get_subclass(script, "Greeter");
	Ref<FoundryScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(greeter.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	FSReflection *reflection = memnew(FSReflection);

	// A valid type + handler produces a working proxy that routes through the handler.
	{
		Ref<RefCounted> proxy = reflection->create_proxy_dynamic(greeter, Callable(recorder, "handle"));
		REQUIRE(proxy.is_valid());
		CHECK(proxy->get_script_instance()->get_script() == greeter);

		Variant subject = "World";
		const Variant *args[1] = { &subject };
		Callable::CallError error;
		Variant result = proxy->get_script_instance()->callp("greet", args, 1, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant("stub:greet"));

		Array calls = recorder->get("calls");
		REQUIRE(calls.size() == 1);
		CHECK(calls[0] == Variant("greet"));
	}

	// Invalid input (a concrete class, or an invalid handler) returns a null Ref; the
	// error is reported rather than crashing.
	{
		Ref<FoundryScript> recorder_as_type = recorder_script;
		ERR_PRINT_OFF;
		Ref<RefCounted> bad_type = reflection->create_proxy_dynamic(recorder_as_type, Callable(recorder, "handle"));
		Ref<RefCounted> bad_handler = reflection->create_proxy_dynamic(greeter, Callable());
		ERR_PRINT_ON;
		CHECK(bad_type.is_null());
		CHECK(bad_handler.is_null());
	}

	memdelete(reflection);
}

TEST_CASE("[Modules][FoundryScript][Proxy] Delegating proxy advises and forwards") {
	ScopedProxyLanguage language;

	const char *source =
			"abstract class Service:\n"
			"\tvar label: String\n"
			"\tabstract func greet(subject: String) -> String\n"
			"\tabstract func add(a: int, b: int) -> int\n"
			"\n"
			"class RealService extends Service:\n"
			"\tfunc greet(subject: String) -> String:\n"
			"\t\treturn \"hello \" + subject\n"
			"\tfunc add(a: int, b: int) -> int:\n"
			"\t\treturn a + b\n"
			"\n"
			"class Advisor:\n"
			"\tvar advised: Array = []\n"
			"\tfunc advise(method_name, args, target):\n"
			"\t\tadvised.append(str(method_name))\n"
			"\t\treturn \"wrapped:\" + str(target.callv(method_name, args))\n";

	Ref<FoundryScript> script = compile_proxy_source(source);
	Ref<FoundryScript> service = get_subclass(script, "Service");
	Ref<FoundryScript> real_service_script = get_subclass(script, "RealService");
	Ref<FoundryScript> advisor_script = get_subclass(script, "Advisor");
	REQUIRE(service.is_valid());

	Callable::CallError construct_error;
	Variant real_ref = real_service_script->_new(nullptr, 0, construct_error);
	REQUIRE(construct_error.error == Callable::CallError::CALL_OK);
	Object *real = real_ref;
	real->set("label", "real-label");

	Variant advisor_ref = advisor_script->_new(nullptr, 0, construct_error);
	Object *advisor = advisor_ref;

	Dictionary interceptor;
	interceptor["greet"] = Callable(advisor, "advise");

	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_delegating_proxy(service, real_ref, interceptor, error_message);
	REQUIRE_MESSAGE(proxy.is_valid(), error_message.utf8().get_data());
	ScriptInstance *instance = proxy->get_script_instance();

	// Advised method: the advice runs and proceeds to the target.
	{
		Variant subject = "world";
		const Variant *args[1] = { &subject };
		Callable::CallError error;
		Variant result = instance->callp("greet", args, 1, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant("wrapped:hello world"));
	}

	// Unadvised method: forwarded straight to the target.
	{
		Variant a = 2;
		Variant b = 3;
		const Variant *args[2] = { &a, &b };
		Callable::CallError error;
		Variant result = instance->callp("add", args, 2, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant(5));
	}

	// Property read/write forwards to the target.
	{
		Variant value;
		CHECK(instance->get("label", value));
		CHECK(value == Variant("real-label"));
		CHECK(instance->set("label", "changed"));
		CHECK(real->get("label") == Variant("changed"));
	}

	// Only the advised method reached the interceptor.
	{
		Array advised = advisor->get("advised");
		REQUIRE(advised.size() == 1);
		CHECK(advised[0] == Variant("greet"));
	}

	// A null target is rejected at construction.
	{
		String message;
		Ref<RefCounted> bad = FSProxy::create_delegating_proxy(service, Variant(), interceptor, message);
		CHECK(bad.is_null());
		CHECK_FALSE(message.is_empty());
	}

	// A target that does not implement the proxied type is rejected.
	{
		String message;
		Ref<RefCounted> bad = FSProxy::create_delegating_proxy(service, advisor_ref, interceptor, message);
		CHECK(bad.is_null());
		CHECK_FALSE(message.is_empty());
	}

	// The target's own argument errors are surfaced, not collapsed to a default.
	{
		Variant only_one = 2; // add() requires two arguments.
		const Variant *args[1] = { &only_one };
		Callable::CallError error;
		ERR_PRINT_OFF;
		instance->callp("add", args, 1, error);
		ERR_PRINT_ON;
		CHECK(error.error == Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS);
	}
}

TEST_CASE("[Modules][FoundryScript][Reflection] godot namespace is a reserved autoload name") {
	ScopedProxyLanguage language;

	FSLanguage *fs_language = FSLanguage::get_singleton();

	// The reflection API is exposed as the `godot` named global constant (see
	// FSLanguage::init), so the language reports `foundry` as a reserved global
	// name. Editor autoload validation iterates this list exactly like
	// get_reserved_words and rejects an autoload that would shadow `foundry.reflection`.
	// The registration and this reservation share a single source constant, so they
	// cannot drift. The reservation is scoped to tools builds, where the compiler can
	// resolve named globals; an exported runtime does not reserve the name.
	const Vector<String> reserved = fs_language->get_reserved_global_names();
#ifdef TOOLS_ENABLED
	CHECK(reserved.has("foundry"));
	CHECK(fs_language->is_reserved_global_name("foundry"));
#else
	CHECK_FALSE(reserved.has("foundry"));
	CHECK_FALSE(fs_language->is_reserved_global_name("foundry"));
#endif

	// A normal identifier is never reserved by this mechanism.
	CHECK_FALSE(reserved.has("my_autoload"));
	CHECK_FALSE(fs_language->is_reserved_global_name("my_autoload"));
}

} // namespace FSTests
