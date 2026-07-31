/**************************************************************************/
/*  test_fs_json_marshal.h                                                */
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

#include "../fs_analyzer.h"
#include "../fs_json_marshal.h"
#include "../fs_parser.h"
#include "../fs_script_extensible_native_hooks.h"
#include "../fs_warning.h"
#include "fs_test_runner.h"
#include "test_analyzer_finalization.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/object/script_language.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

// Declared in the global namespace because of the FOUNDRY_CLASS macro warning on Windows:
// "Unqualified friend declaration referring to type outside of the nearest enclosing namespace
// is a Microsoft extension; add a nested name specifier".
//
// A native class that binds `to_json` is what makes the hook exemption observable: without one,
// no script declaration of `to_json` has a native parent signature to be warned about.
class FSJsonMarshalTestHost : public RefCounted {
	FOUNDRY_CLASS(FSJsonMarshalTestHost, RefCounted);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("to_json"), &FSJsonMarshalTestHost::to_json);
	}

public:
	String to_json() const { return "from native"; }
};

namespace FSTests {

static const char *json_marshal_scripts_directory = "modules/foundry_script/tests/scripts";

struct JsonMarshalProjectFixture {
	TestProjectSettingsRestoreScope project_settings;

	JsonMarshalProjectFixture() {
		init_language(String(json_marshal_scripts_directory));
		REQUIRE_MESSAGE(is_fs_language_active(), "Failed to initialize the marshal test project.");
	}
};

static Ref<RefCounted> instantiate_script_host(const String &p_res_path) {
	const Ref<Script> host_script = ResourceLoader::load(p_res_path);
	REQUIRE_MESSAGE(host_script.is_valid(), vformat("Failed to load host script: %s", p_res_path));
	REQUIRE(host_script->is_valid());
	REQUIRE(host_script->can_instantiate());

	Ref<RefCounted> host(memnew(RefCounted));
	host->set_script(host_script);
	return host;
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] The to_json hook is registered as script-extensible") {
	// `to_json` is implemented in script, so declaring it must not be reported as an accidental
	// shadow of a native method.
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Object"), SNAME("to_json")));

	// The exemption reaches native subclasses of the registered base, since any object may
	// conform to `JsonSerializable`.
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("RefCounted"), SNAME("to_json")));
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Node"), SNAME("to_json")));

	// Unrelated native methods keep warning: the exemption is scoped to the hook name.
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Object"), SNAME("get")));
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("RefCounted"), SNAME("reference")));
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Node"), SNAME("to_string")));

	// The hook is synchronous; an async override is not silently accepted.
	CHECK_FALSE(FSScriptExtensibleNativeHooks::allows_async_override_of_sync_hook(
			SNAME("Object"), SNAME("to_json")));
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] The hook is offered to a native base that binds it") {
	// `collect_allowed_overrides` feeds the editor's override-method list, which only offers a
	// hook the native base actually declares.
	List<StringName> hook_method_names;
	FSScriptExtensibleNativeHooks::collect_allowed_overrides(SNAME("RefCounted"), hook_method_names);
	CHECK(hook_method_names.find(SNAME("to_json")) != nullptr);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Calling the hook on a null object fails") {
	Variant node = "untouched";
	ERR_PRINT_OFF;
	CHECK_FALSE(FSJsonMarshal::call_to_json(nullptr, node));
	ERR_PRINT_ON;
	CHECK_EQ(String(node), "untouched");
	CHECK_FALSE(FSJsonMarshal::has_to_json(nullptr));
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Native implementations dispatch by name") {
	// A native subclass of the extensible base still works: the helper resolves the bound
	// method through ClassDB rather than requiring a script.
	Ref<FSJsonMarshalTestHost> native_host(memnew(FSJsonMarshalTestHost));
	CHECK(FSJsonMarshal::has_to_json(native_host.ptr()));

	Variant node;
	CHECK(FSJsonMarshal::call_to_json(native_host.ptr(), node));
	CHECK_EQ(String(node), "from native");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] An object without the hook reports failure") {
	Ref<RefCounted> bare_host(memnew(RefCounted));
	CHECK_FALSE(FSJsonMarshal::has_to_json(bare_host.ptr()));

	Variant node = "untouched";
	ERR_PRINT_OFF;
	CHECK_FALSE(FSJsonMarshal::call_to_json(bare_host.ptr(), node));
	ERR_PRINT_ON;
	CHECK_EQ(String(node), "untouched");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Native code dispatches to a script implementation") {
	JsonMarshalProjectFixture project;

	const Ref<RefCounted> host =
			instantiate_script_host("res://json_marshal_host/serializable_host.notest.fs");
	CHECK(FSJsonMarshal::has_to_json(host.ptr()));

	Variant node;
	REQUIRE(FSJsonMarshal::call_to_json(host.ptr(), node));

	// `JsonNode.Str("from script")` lowers to the tagged-union representation `[tag, payload]`,
	// where `Str` is case 4 in the wire contract pinned by the builtin type tests.
	REQUIRE_EQ(node.get_type(), Variant::ARRAY);
	const Array encoded = node;
	REQUIRE_EQ(encoded.size(), 2);
	CHECK_EQ(int(encoded[0]), 4);
	CHECK_EQ(String(encoded[1]), "from script");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A script without the hook reports failure") {
	JsonMarshalProjectFixture project;

	const Ref<RefCounted> host = instantiate_script_host("res://json_marshal_host/plain_host.notest.fs");
	CHECK_FALSE(FSJsonMarshal::has_to_json(host.ptr()));

	Variant node = "untouched";
	ERR_PRINT_OFF;
	CHECK_FALSE(FSJsonMarshal::call_to_json(host.ptr(), node));
	ERR_PRINT_ON;
	CHECK_EQ(String(node), "untouched");
}

#if defined(DEBUG_ENABLED) && defined(TOOLS_ENABLED)
static void analyze_against_native_host(const char *p_source, const String &p_path, FSParser &r_parser) {
	// Registering the native base is what gives a script declaration of `to_json` a parent
	// signature to be compared against, and the analyzer only resolves `extends` against an
	// exposed class, so force full registration before analysis.
	FOUNDRY_REGISTER_CLASS(FSJsonMarshalTestHost);
	REQUIRE(ClassDB::is_class_exposed(SNAME("FSJsonMarshalTestHost")));

	REQUIRE_EQ(r_parser.parse(p_source, p_path, false), OK);
	FSAnalyzer analyzer(&r_parser);
	const Error analyze_result = analyzer.analyze();
	String reported_errors;
	for (const FSParser::ParserError &parse_error : r_parser.get_errors()) {
		reported_errors += "\n" + parse_error.message;
	}
	REQUIRE_MESSAGE(analyze_result == OK, reported_errors);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Implementing the hook is not a native-override warning") {
	AnalyzerWarningSettingsScope warning_settings;

	const char *source = R"(
extends FSJsonMarshalTestHost

func to_json() -> String:
	return "from script"

func test() -> void:
	pass
)";

	FSParser parser;
	analyze_against_native_host(source, "user://json_marshal_hook_override.fs", parser);
	CHECK_EQ(count_warnings_with_code(parser, FSWarning::NATIVE_METHOD_OVERRIDE), 0);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] An unrelated native override still warns") {
	AnalyzerWarningSettingsScope warning_settings;

	const char *source = R"(
extends FSJsonMarshalTestHost

func get(_property: StringName) -> Variant:
	return null

func test() -> void:
	pass
)";

	FSParser parser;
	analyze_against_native_host(source, "user://json_marshal_unrelated_override.fs", parser);
	CHECK_EQ(count_warnings_with_code(parser, FSWarning::NATIVE_METHOD_OVERRIDE), 1);
}
#endif // DEBUG_ENABLED && TOOLS_ENABLED

} // namespace FSTests
