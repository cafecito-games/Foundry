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
#include "../gdscript_reflection.h"

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
	// Synthetic flag keeps language-keyed casts (e.g. inst_to_dict) from treating
	// the proxy as a GDScriptInstance.
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

TEST_CASE("[Modules][GDScript][Proxy] Auto-backing property store") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Bag:\n"
			"\tvar label: String\n"
			"\tvar count: int\n"
			"\tvar ratio: float\n"
			"\tvar items: Array\n"
			"\tvar tags: Array[int]\n"
			"\tvar scores: Dictionary[String, int]\n"
			"\t@abstract func use() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tvar calls: Array = []\n"
			"\tfunc handle(method_name, args):\n"
			"\t\tcalls.append(str(method_name))\n"
			"\t\treturn null\n";

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> bag = get_subclass(script, "Bag");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(bag.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(bag, Callable(recorder, "handle"), error_message);
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

TEST_CASE("[Modules][GDScript][Proxy] is / trait conformance") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Drawable:\n"
			"\t@abstract func draw_self() -> void\n"
			"\n"
			"trait Sprite uses Drawable:\n"
			"\t@abstract func render() -> void\n"
			"\n"
			"trait Unrelated:\n"
			"\t@abstract func z() -> void\n"
			"\n"
			"@abstract class Base:\n"
			"\t@abstract func a() -> void\n"
			"\n"
			"@abstract class Derived extends Base:\n"
			"\t@abstract func b() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> sprite = get_subclass(script, "Sprite");
	Ref<GDScript> drawable = get_subclass(script, "Drawable");
	Ref<GDScript> unrelated = get_subclass(script, "Unrelated");
	Ref<GDScript> base = get_subclass(script, "Base");
	Ref<GDScript> derived = get_subclass(script, "Derived");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
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
	Ref<RefCounted> sprite_proxy = GDScriptProxy::create_proxy(sprite, Callable(recorder, "handle"), error_message);
	REQUIRE_MESSAGE(sprite_proxy.is_valid(), error_message.utf8().get_data());
	CHECK(sprite_proxy->get_script_instance()->get_script() == sprite);

	// An abstract-class proxy's script chain contains the class and its bases.
	Ref<RefCounted> derived_proxy = GDScriptProxy::create_proxy(derived, Callable(recorder, "handle"), error_message);
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

TEST_CASE("[Modules][GDScript][Proxy] Handler return coercion and validation") {
	ScopedProxyLanguage language;

	const char *source =
			"@abstract class Service:\n"
			"\t@abstract func get_count() -> int\n"
			"\t@abstract func get_ratio() -> float\n"
			"\t@abstract func get_label() -> String\n"
			"\t@abstract func do_nothing() -> void\n"
			"\t@abstract func get_anything() -> Variant\n"
			"\t@abstract func get_tags() -> Array[int]\n"
			"\t@abstract func get_scores() -> Dictionary[String, int]\n"
			"\n"
			"class Recorder:\n"
			"\tvar stub_return: Variant = null\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn stub_return\n";

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> service = get_subclass(script, "Service");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(service.is_valid());

	Callable::CallError construct_error;
	Variant recorder_ref = recorder_script->_new(nullptr, -1, construct_error);
	Object *recorder = recorder_ref;

	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(service, Callable(recorder, "handle"), error_message);
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
		Ref<RefCounted> broken_proxy = GDScriptProxy::create_proxy(service, Callable(throwaway, "handle"), message);
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

TEST_CASE("[Modules][GDScript][Reflection] Read-only introspection API") {
	ScopedProxyLanguage language;

	const char *source =
			"trait Drawable:\n"
			"\t@abstract func draw_self() -> void\n"
			"\n"
			"trait Sprite uses Drawable:\n"
			"\tvar label: String\n"
			"\tvar count: int\n"
			"\t@abstract func render() -> void\n"
			"\tfunc describe() -> String:\n"
			"\t\treturn \"sprite\"\n"
			"\n"
			"trait Unrelated:\n"
			"\t@abstract func z() -> void\n"
			"\n"
			"class Recorder:\n"
			"\tfunc handle(method_name, args):\n"
			"\t\treturn null\n";

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> sprite = get_subclass(script, "Sprite");
	Ref<GDScript> drawable = get_subclass(script, "Drawable");
	Ref<GDScript> unrelated = get_subclass(script, "Unrelated");
	Ref<GDScript> recorder_script = get_subclass(script, "Recorder");
	REQUIRE(sprite.is_valid());

	GDScriptReflection *reflection = memnew(GDScriptReflection);

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
		Ref<RefCounted> proxy = GDScriptProxy::create_proxy(sprite, Callable(recorder, "handle"), error_message);
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

TEST_CASE("[Modules][GDScript][Proxy] Delegating proxy advises and forwards") {
	ScopedProxyLanguage language;

	const char *source =
			"@abstract class Service:\n"
			"\tvar label: String\n"
			"\t@abstract func greet(subject: String) -> String\n"
			"\t@abstract func add(a: int, b: int) -> int\n"
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

	Ref<GDScript> script = compile_proxy_source(source);
	Ref<GDScript> service = get_subclass(script, "Service");
	Ref<GDScript> real_service_script = get_subclass(script, "RealService");
	Ref<GDScript> advisor_script = get_subclass(script, "Advisor");
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
	Ref<RefCounted> proxy = GDScriptProxy::create_delegating_proxy(service, real_ref, interceptor, error_message);
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
		Ref<RefCounted> bad = GDScriptProxy::create_delegating_proxy(service, Variant(), interceptor, message);
		CHECK(bad.is_null());
		CHECK_FALSE(message.is_empty());
	}
}

} // namespace GDScriptTests
