/**************************************************************************/
/*  test_gdscript.cpp                                                     */
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

#include "test_gdscript.h"

#include "../editor/gdscript_docgen.h"
#include "../gdscript.h"
#include "../gdscript_analyzer.h"
#include "../gdscript_compiler.h"
#include "../gdscript_parser.h"
#include "../gdscript_tokenizer.h"
#include "../gdscript_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "tests/test_tools.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_settings.h"
#endif

namespace GDScriptTests {

TEST_CASE("[Modules][GDScript] Language reserved words include namespace declarations") {
	Vector<String> reserved_words = GDScriptLanguage::get_singleton()->get_reserved_words();
	CHECK(reserved_words.has("import"));
	CHECK(reserved_words.has("namespace"));
}

static PackedStringArray parse_source_errors(const String &p_source) {
	GDScriptParser parser;
	Error err = parser.parse(p_source, "user://namespace_import_test.gd", false);
	PackedStringArray errors;
	if (err == OK) {
		return errors;
	}

	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		errors.push_back(parser_error.message);
	}
	return errors;
}

static void check_parse_source_error(const String &p_source, const String &p_expected_error) {
	INFO(p_source);
	PackedStringArray errors = parse_source_errors(p_source);
	CHECK_EQ(errors.size(), 1);
	if (errors.size() != 1) {
		return;
	}
	CHECK_EQ(errors[0], p_expected_error);
}

static const GDScriptParser::FunctionNode *find_parser_function(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type != GDScriptParser::ClassNode::Member::FUNCTION || member.function == nullptr ||
				member.function->identifier == nullptr) {
			continue;
		}
		if (member.function->identifier->name == p_name) {
			return member.function;
		}
	}

	return nullptr;
}

static const GDScriptParser::ClassNode *find_parser_class(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type != GDScriptParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
				member.m_class->identifier == nullptr) {
			continue;
		}
		if (member.m_class->identifier->name == p_name) {
			return member.m_class;
		}
	}

	return nullptr;
}

static String write_temp_script(const String &p_file_name, const String &p_source) {
	Error err = OK;
	Ref<FileAccess> file = FileAccess::create_temp(FileAccess::WRITE, p_file_name.get_basename(), "gd", true, &err);
	CHECK_EQ(err, OK);
	CHECK(file.is_valid());
	if (file.is_valid()) {
		file->store_string(p_source);
		return file->get_path_absolute();
	}
	return String();
}

struct TempScriptFile {
	String path;

	TempScriptFile(const String &p_file_name, const String &p_source) {
		path = write_temp_script(p_file_name, p_source);
	}

	~TempScriptFile() {
		if (!path.is_empty()) {
			DirAccess::remove_absolute(path);
		}
	}
};

static void restore_global_script_classes(const Array &p_classes) {
	ScriptServer::global_classes_clear();
	for (const Variant &script_class : p_classes) {
		Dictionary c = script_class;
		if (!c.has("class") || !c.has("language") || !c.has("path") || !c.has("base") ||
				!c.has("is_abstract") || !c.has("is_tool")) {
			continue;
		}
		ScriptServer::add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"]);
	}
	ProjectSettings::get_singleton()->store_global_class_list(p_classes);
}

struct GlobalScriptClassCacheBackup {
	Array classes;
	String cache_path;
	String cache_contents;
	bool cache_existed = false;

	GlobalScriptClassCacheBackup() {
		classes = ProjectSettings::get_singleton()->get_global_class_list();
		cache_path = ProjectSettings::get_singleton()->get_global_class_list_path();
		cache_existed = FileAccess::exists(cache_path);
		if (cache_existed) {
			Ref<FileAccess> file = FileAccess::open(cache_path, FileAccess::READ);
			if (file.is_valid()) {
				cache_contents = file->get_as_utf8_string();
			}
		}
	}

	~GlobalScriptClassCacheBackup() {
		restore_global_script_classes(classes);
		if (cache_existed) {
			Ref<FileAccess> file = FileAccess::open(cache_path, FileAccess::WRITE);
			if (file.is_valid()) {
				file->store_string(cache_contents);
			}
		} else {
			DirAccess::remove_absolute(cache_path);
		}
	}
};

TEST_CASE("[Modules][GDScript] Parser stores namespace and import declarations") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace characters.controllers
import characters
import characters.stats
@abstract
class_name MyCharacterController
extends Node
)",
			"user://my_character_controller.gd", false);

	CHECK(err == OK);
	if (err != OK) {
		return;
	}
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->identifier != nullptr);
	if (root->identifier == nullptr) {
		return;
	}

	CHECK_EQ(root->namespace_name, "characters.controllers");
	CHECK_EQ(root->qualified_global_name, "characters.controllers.MyCharacterController");
	CHECK_EQ(root->fqcn, "characters.controllers.MyCharacterController");
	CHECK_EQ(root->identifier->name, SNAME("MyCharacterController"));
	CHECK(root->is_abstract);
	CHECK_EQ(root->imports.size(), 2);
	if (root->imports.size() != 2) {
		return;
	}
	CHECK_EQ(root->imports[0], "characters");
	CHECK_EQ(root->imports[1], "characters.stats");
}

TEST_CASE("[Modules][GDScript] Parser accepts imports without namespace") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
import shared
import shared.types
class_name UsesShared
)",
			"user://uses_shared.gd", false);

	CHECK(err == OK);
	if (err != OK) {
		return;
	}
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->identifier != nullptr);
	if (root->identifier == nullptr) {
		return;
	}

	CHECK(root->namespace_name.is_empty());
	CHECK_EQ(root->qualified_global_name, "UsesShared");
	CHECK_EQ(root->fqcn, "UsesShared");
	CHECK_EQ(root->imports.size(), 2);
	if (root->imports.size() != 2) {
		return;
	}
	CHECK_EQ(root->imports[0], "shared");
	CHECK_EQ(root->imports[1], "shared.types");
}

TEST_CASE("[Modules][GDScript] Parser accepts contextual async function modifiers") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
@abstract
class_name AsyncParserContract

async func load() -> int:
	return 1

static async func make() -> int:
	return 2

@abstract async func download_data() -> String

@rpc async func remote_load() -> void:
	pass

@rpc static async func remote_make() -> void:
	pass

class InlineAsync: async func tick() -> void: pass

class InlineStatic: static func make() -> int: return 3
)",
			"user://async_parser_contract.gd", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const GDScriptParser::FunctionNode *load = find_parser_function(root, SNAME("load"));
	CHECK(load != nullptr);
	if (load == nullptr) {
		return;
	}
	CHECK(load->is_declared_async);
	CHECK(load->is_coroutine);
	CHECK(!load->is_static);
#ifdef TOOLS_ENABLED
	CHECK_EQ(load->signature, "() -> int");
#endif // TOOLS_ENABLED

	const GDScriptParser::FunctionNode *make = find_parser_function(root, SNAME("make"));
	CHECK(make != nullptr);
	if (make == nullptr) {
		return;
	}
	CHECK(make->is_declared_async);
	CHECK(make->is_coroutine);
	CHECK(make->is_static);
#ifdef TOOLS_ENABLED
	CHECK_EQ(make->signature, "() -> int");
#endif // TOOLS_ENABLED

	const GDScriptParser::FunctionNode *download_data = find_parser_function(root, SNAME("download_data"));
	CHECK(download_data != nullptr);
	if (download_data == nullptr) {
		return;
	}
	CHECK(download_data->is_declared_async);
	CHECK(download_data->is_coroutine);
	CHECK(!download_data->is_static);
#ifdef TOOLS_ENABLED
	CHECK_EQ(download_data->signature, "() -> String");
#endif // TOOLS_ENABLED

	const GDScriptParser::FunctionNode *remote_load = find_parser_function(root, SNAME("remote_load"));
	CHECK(remote_load != nullptr);
	if (remote_load == nullptr) {
		return;
	}
	CHECK(remote_load->is_declared_async);
	CHECK(remote_load->is_coroutine);
	CHECK(!remote_load->is_static);

	const GDScriptParser::FunctionNode *remote_make = find_parser_function(root, SNAME("remote_make"));
	CHECK(remote_make != nullptr);
	if (remote_make == nullptr) {
		return;
	}
	CHECK(remote_make->is_declared_async);
	CHECK(remote_make->is_coroutine);
	CHECK(remote_make->is_static);

	const GDScriptParser::ClassNode *inline_async = find_parser_class(root, SNAME("InlineAsync"));
	CHECK(inline_async != nullptr);
	if (inline_async == nullptr) {
		return;
	}
	const GDScriptParser::FunctionNode *tick = find_parser_function(inline_async, SNAME("tick"));
	CHECK(tick != nullptr);
	if (tick == nullptr) {
		return;
	}
	CHECK(tick->is_declared_async);
	CHECK(tick->is_coroutine);
	CHECK(!tick->is_static);

	const GDScriptParser::ClassNode *inline_static = find_parser_class(root, SNAME("InlineStatic"));
	CHECK(inline_static != nullptr);
	if (inline_static == nullptr) {
		return;
	}
	const GDScriptParser::FunctionNode *inline_make = find_parser_function(inline_static, SNAME("make"));
	CHECK(inline_make != nullptr);
	if (inline_make == nullptr) {
		return;
	}
	CHECK(!inline_make->is_declared_async);
	CHECK(!inline_make->is_coroutine);
	CHECK(inline_make->is_static);
}

TEST_CASE("[Modules][GDScript] Parser rejects invalid async function modifier positions") {
	check_parse_source_error(R"(
async static func load() -> void:
	pass
)",
			R"("static" must appear before "async" in a static async function declaration.)");

	check_parse_source_error(R"(
func async load() -> void:
	pass
)",
			R"("async" must appear before "func" when used as a function modifier.)");

	check_parse_source_error(R"(
async var value = 1
)",
			R"("async" can only be used as a function modifier before "func".)");

	GDScriptParser parser;
	Error err = parser.parse(R"(
func async load() -> void:
	pass

func after() -> void:
	pass
)",
			"user://async_recovery.gd", false);
	CHECK(err != OK);
	CHECK_EQ(parser.get_errors().size(), 1);

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(find_parser_function(root, SNAME("after")) != nullptr);

	GDScriptParser inline_parser;
	Error inline_err = inline_parser.parse(R"(
class BrokenInline: static async var value = 1

func after_inline() -> void:
	pass
)",
			"user://async_inline_recovery.gd", false);
	CHECK(inline_err != OK);
	CHECK_GE(inline_parser.get_errors().size(), 1);

	const GDScriptParser::ClassNode *inline_root = inline_parser.get_tree();
	CHECK(inline_root != nullptr);
	if (inline_root == nullptr) {
		return;
	}
	CHECK(find_parser_function(inline_root, SNAME("after_inline")) != nullptr);
}

TEST_CASE("[Modules][GDScript] Parser keeps async usable as an identifier outside modifier positions") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
func async() -> int:
	var async = 1
	return async
)",
			"user://async_identifier.gd", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	const GDScriptParser::FunctionNode *async_function = find_parser_function(root, SNAME("async"));
	CHECK(async_function != nullptr);
	if (async_function == nullptr) {
		return;
	}
	CHECK(!async_function->is_declared_async);
	CHECK(!async_function->is_coroutine);
}

TEST_CASE("[Modules][GDScript] Analyzer marks coroutine function metadata as async") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
async func explicit_async() -> int:
	return 1

func body_inferred_async() -> int:
	@warning_ignore("redundant_await")
	await 0
	return 2

func synchronous() -> int:
	return 3
)",
			"user://async_metadata.gd", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const GDScriptParser::FunctionNode *explicit_async = find_parser_function(root, SNAME("explicit_async"));
	CHECK(explicit_async != nullptr);
	if (explicit_async == nullptr) {
		return;
	}
	CHECK((explicit_async->info.flags & METHOD_FLAG_ASYNC) != 0);

	const GDScriptParser::FunctionNode *body_inferred_async = find_parser_function(root, SNAME("body_inferred_async"));
	CHECK(body_inferred_async != nullptr);
	if (body_inferred_async == nullptr) {
		return;
	}
	CHECK((body_inferred_async->info.flags & METHOD_FLAG_ASYNC) != 0);

	const GDScriptParser::FunctionNode *synchronous = find_parser_function(root, SNAME("synchronous"));
	CHECK(synchronous != nullptr);
	if (synchronous == nullptr) {
		return;
	}
	CHECK_FALSE((synchronous->info.flags & METHOD_FLAG_ASYNC) != 0);
}

TEST_CASE("[Modules][GDScript] Parser rejects invalid namespace and import declarations") {
	check_parse_source_error(R"(
namespace first
namespace second
class_name DuplicateNamespace
)",
			R"("namespace" can only be used once.)");

	check_parse_source_error(R"(
namespace characters.
class_name MalformedNamespace
)",
			R"(Expected identifier after "." in namespace declaration.)");

	check_parse_source_error(R"(
import .characters
class_name MalformedImport
)",
			R"(Expected identifier after "import".)");

	check_parse_source_error(R"(
import characters
namespace characters.controllers
class_name NamespaceAfterImport
)",
			R"("namespace" must be declared before "import".)");

	check_parse_source_error(R"(
@abstract
namespace characters.controllers
class_name ClassAnnotationBeforeNamespace
)",
			R"(Class annotations must appear after "namespace" and "import" declarations.)");

	{
		PackedStringArray errors = parse_source_errors(R"(
@abstract
namespace characters.controllers import characters
class_name ClassAnnotationBeforeMalformedNamespace
)");
		CHECK_EQ(errors.size(), 2);
		if (errors.size() == 2) {
			CHECK_EQ(errors[0], R"(Class annotations must appear after "namespace" and "import" declarations.)");
			CHECK_EQ(errors[1], R"(Expected end of statement after namespace declaration, found "import" instead.)");
		}
	}

	check_parse_source_error(R"(
namespace characters.controllers
@tool
class_name ScriptAnnotationAfterNamespace
)",
			R"(Annotation "@tool" must be at the top of the script, before "extends" and "class_name".)");

	check_parse_source_error(R"(
class_name ImportAfterClassName
import characters
)",
			R"("import" declarations must appear before "class_name", "extends", and body declarations.)");
}

TEST_CASE("[Modules][GDScript] Global class names use namespace-qualified identity") {
	TempScriptFile script("qualified_global_name.gd", R"(
namespace characters
class_name BaseCharacter
extends Node
)");

	String base_type;
	bool is_abstract = true;
	bool is_tool = true;
	String class_name = GDScriptLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr, &is_abstract, &is_tool);

	CHECK_EQ(class_name, "characters.BaseCharacter");
	CHECK_EQ(base_type, "Node");
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
}

TEST_CASE("[Modules][GDScript] Global namespace class names stay unqualified") {
	TempScriptFile script("unqualified_global_name.gd", R"(
class_name PlainCharacter
extends Node
)");

	String class_name = GDScriptLanguage::get_singleton()->get_global_class_name(script.path);

	CHECK_EQ(class_name, "PlainCharacter");
}

TEST_CASE("[Modules][GDScript] Loaded namespaced global class keeps qualified runtime identity") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name RuntimeCharacter
)",
			"user://qualified_runtime_global_name.gd", false);
	CHECK_EQ(err, OK);

	Ref<GDScript> compiled;
	compiled.instantiate();
	GDScriptCompiler::make_scripts(compiled.ptr(), parser.get_tree(), false);
	ScriptServer::add_global_class("characters.RuntimeCharacter", "RefCounted",
			GDScriptLanguage::get_singleton()->get_name(), "user://qualified_runtime_global_name.gd", false, false);

	CHECK_EQ(compiled->get_global_name(), "characters.RuntimeCharacter");
	CHECK(ScriptServer::is_global_class(compiled->get_global_name()));
}

TEST_CASE("[Modules][GDScript] Namespaced global class property metadata uses qualified names") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name PropertyTarget

var direct: PropertyTarget
var list: Array[PropertyTarget]
var map: Dictionary[String, PropertyTarget]
)",
			"user://qualified_property_metadata.gd", false);
	CHECK_EQ(err, OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr || root->members.size() < 3) {
		return;
	}

	CHECK_EQ(root->get_global_name(), "characters.PropertyTarget");

	const PropertyInfo direct = root->members[0].variable->get_datatype().to_property_info("direct");
	CHECK_EQ(direct.type, Variant::OBJECT);
	CHECK_EQ(direct.class_name, "characters.PropertyTarget");

	const PropertyInfo list = root->members[1].variable->get_datatype().to_property_info("list");
	CHECK_EQ(list.type, Variant::ARRAY);
	CHECK_EQ(list.hint, PROPERTY_HINT_ARRAY_TYPE);
	CHECK_EQ(list.hint_string, "characters.PropertyTarget");

	const PropertyInfo map = root->members[2].variable->get_datatype().to_property_info("map");
	CHECK_EQ(map.type, Variant::DICTIONARY);
	CHECK_EQ(map.hint, PROPERTY_HINT_DICTIONARY_TYPE);
	CHECK_EQ(map.hint_string, "String;characters.PropertyTarget");
}

TEST_CASE("[Modules][GDScript] Docgen emits qualified names for namespaced global class types") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name DocTarget

var direct: DocTarget
var list: Array[DocTarget]
var map: Dictionary[String, DocTarget]
)",
			"user://qualified_docgen_types.gd", false);
	CHECK_EQ(err, OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<GDScript> script;
	script.instantiate();
	GDScriptCompiler::make_scripts(script.ptr(), root, false);
	GDScriptDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK_EQ(docs.size(), 1);
	if (docs.size() != 1 || docs[0].properties.size() < 3) {
		return;
	}

	CHECK_EQ(docs[0].properties[0].type, "characters.DocTarget");
	CHECK_EQ(docs[0].properties[1].type, "characters.DocTarget[]");
	CHECK_EQ(docs[0].properties[2].type, "Dictionary[String, characters.DocTarget]");
}

TEST_CASE("[Modules][GDScript] Namespaced global classes can share a local name") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.Controller", "Node", GDScriptLanguage::get_singleton()->get_name(), "res://characters/controller.gd", false, false);
	ScriptServer::add_global_class("ui.Controller", "Node", GDScriptLanguage::get_singleton()->get_name(), "res://ui/controller.gd", false, false);

	CHECK(ScriptServer::is_global_class("characters.Controller"));
	CHECK(ScriptServer::is_global_class("ui.Controller"));
	CHECK_EQ(ScriptServer::get_global_class_path("characters.Controller"), "res://characters/controller.gd");
	CHECK_EQ(ScriptServer::get_global_class_path("ui.Controller"), "res://ui/controller.gd");
}

TEST_CASE("[Modules][GDScript] Global script class cache saves qualified class names") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->store_global_class_list(Array());

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/base_character.gd", true, false);
	ScriptServer::save_global_classes();

	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	CHECK_EQ(script_classes.size(), 1);
	if (script_classes.size() == 1) {
		Dictionary script_class = script_classes[0];
		CHECK_EQ(String(script_class["class"]), "characters.BaseCharacter");
		CHECK_FALSE(script_class.has("namespace"));
		CHECK_FALSE(script_class.has("class_name"));
	}
}

TEST_CASE("[Modules][GDScript] Old global script class cache entries still load") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	Dictionary old_script_class;
	old_script_class["class"] = "LegacyCharacter";
	old_script_class["language"] = GDScriptLanguage::get_singleton()->get_name();
	old_script_class["path"] = "res://legacy_character.gd";
	old_script_class["base"] = "Node";
	old_script_class["is_abstract"] = false;
	old_script_class["is_tool"] = false;

	Array old_cache;
	old_cache.push_back(old_script_class);
	ProjectSettings::get_singleton()->store_global_class_list(old_cache);

	ProjectSettings::get_singleton()->refresh_global_class_list();

	CHECK(ScriptServer::is_global_class("LegacyCharacter"));
	CHECK_EQ(ScriptServer::get_global_class_path("LegacyCharacter"), "res://legacy_character.gd");
}

TEST_CASE("[Modules][GDScript] Fully qualified global class collisions keep both paths visible") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/base_character.gd", false, false);

	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name BaseCharacter
extends Node
)",
			"res://duplicates/base_character.gd", false);
	CHECK_EQ(err, OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_NE(err, OK);
	const String expected_error = R"(Class "characters.BaseCharacter" from "res://duplicates/base_character.gd" )"
								  "collides with global script class from \"res://characters/base_character.gd\".";
	bool found_expected_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message == expected_error) {
			found_expected_error = true;
			break;
		}
	}
	CHECK(found_expected_error);
	CHECK_EQ(ScriptServer::get_global_class_path("characters.BaseCharacter"), "res://characters/base_character.gd");
}

TEST_CASE("[Modules][GDScript] Global class re-registration can update the script path") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.MovedCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/old_path.gd", false, false);
	ScriptServer::add_global_class("characters.MovedCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/new_path.gd", false, false);

	CHECK_EQ(ScriptServer::get_global_class_path("characters.MovedCharacter"), "res://characters/new_path.gd");
}

static void test_tokenizer(const String &p_code, const Vector<String> &p_lines) {
	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(p_code);

	int tab_size = 4;
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton()) {
		tab_size = EditorSettings::get_singleton()->get_setting("text_editor/behavior/indent/size");
	}
#endif // TOOLS_ENABLED
	String tab = String(" ").repeat(tab_size);

	GDScriptTokenizer::Token current = tokenizer.scan();
	while (current.type != GDScriptTokenizer::Token::TK_EOF) {
		StringBuilder token;
		token += " --> "; // Padding for line number.

		if (current.start_line != current.end_line) {
			// Print "vvvvvv" to point at the token.
			StringBuilder pointer;
			pointer += "     "; // Padding for line number.

			int line_width = 0;
			if (current.start_line - 1 >= 0 && current.start_line - 1 < p_lines.size()) {
				line_width = p_lines[current.start_line - 1].replace("\t", tab).length();
			}

			const int offset = MAX(0, current.start_column - 1);
			const int width = MAX(0, line_width - current.start_column + 1);
			pointer += String::chr(' ').repeat(offset) + String::chr('v').repeat(width);

			print_line(pointer.as_string());
		}

		for (int l = current.start_line; l <= current.end_line && l <= p_lines.size(); l++) {
			print_line(vformat("%04d %s", l, p_lines[l - 1]).replace("\t", tab));
		}

		{
			// Print "^^^^^^" to point at the token.
			StringBuilder pointer;
			pointer += "     "; // Padding for line number.

			if (current.start_line == current.end_line) {
				const int offset = MAX(0, current.start_column - 1);
				const int width = MAX(0, current.end_column - current.start_column);
				pointer += String::chr(' ').repeat(offset) + String::chr('^').repeat(width);
			} else {
				const int width = MAX(0, current.end_column - 1);
				pointer += String::chr('^').repeat(width);
			}

			print_line(pointer.as_string());
		}

		token += current.get_name();

		if (current.type == GDScriptTokenizer::Token::ERROR || current.type == GDScriptTokenizer::Token::LITERAL || current.type == GDScriptTokenizer::Token::IDENTIFIER || current.type == GDScriptTokenizer::Token::ANNOTATION) {
			token += "(";
			token += Variant::get_type_name(current.literal.get_type());
			token += ") ";
			token += current.literal;
		}

		print_line(token.as_string());

		print_line("-------------------------------------------------------");

		current = tokenizer.scan();
	}

	print_line(current.get_name()); // Should be EOF
}

static void test_tokenizer_buffer(const Vector<uint8_t> &p_buffer, const Vector<String> &p_lines);

static void test_tokenizer_buffer(const String &p_code, const Vector<String> &p_lines) {
	Vector<uint8_t> binary = GDScriptTokenizerBuffer::parse_code_string(p_code, GDScriptTokenizerBuffer::COMPRESS_NONE);
	test_tokenizer_buffer(binary, p_lines);
}

static void test_tokenizer_buffer(const Vector<uint8_t> &p_buffer, const Vector<String> &p_lines) {
	GDScriptTokenizerBuffer tokenizer;
	tokenizer.set_code_buffer(p_buffer);

	int tab_size = 4;
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton()) {
		tab_size = EditorSettings::get_singleton()->get_setting("text_editor/behavior/indent/size");
	}
#endif // TOOLS_ENABLED
	String tab = String(" ").repeat(tab_size);

	GDScriptTokenizer::Token current = tokenizer.scan();
	while (current.type != GDScriptTokenizer::Token::TK_EOF) {
		StringBuilder token;
		token += " --> "; // Padding for line number.

		for (int l = current.start_line; l <= current.end_line && l <= p_lines.size(); l++) {
			print_line(vformat("%04d %s", l, p_lines[l - 1]).replace("\t", tab));
		}

		token += current.get_name();

		if (current.type == GDScriptTokenizer::Token::ERROR || current.type == GDScriptTokenizer::Token::LITERAL || current.type == GDScriptTokenizer::Token::IDENTIFIER || current.type == GDScriptTokenizer::Token::ANNOTATION) {
			token += "(";
			token += Variant::get_type_name(current.literal.get_type());
			token += ") ";
			token += current.literal;
		}

		print_line(token.as_string());

		print_line("-------------------------------------------------------");

		current = tokenizer.scan();
	}

	print_line(current.get_name()); // Should be EOF
}

static void test_parser(const String &p_code, const String &p_script_path, const Vector<String> &p_lines) {
	GDScriptParser parser;
	Error err = parser.parse(p_code, p_script_path, false);

	if (err != OK) {
		const List<GDScriptParser::ParserError> &errors = parser.get_errors();
		for (const GDScriptParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
	}

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	if (err != OK) {
		const List<GDScriptParser::ParserError> &errors = parser.get_errors();
		for (const GDScriptParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
	}

#ifdef TOOLS_ENABLED
	GDScriptParser::TreePrinter printer;
	printer.print_tree(parser);
#endif
}

static void disassemble_function(const GDScriptFunction *p_func, const Vector<String> &p_lines) {
	ERR_FAIL_NULL(p_func);

	String arg_string;
	bool is_first_arg = true;
	for (const PropertyInfo &arg_info : p_func->get_method_info().arguments) {
		if (!is_first_arg) {
			arg_string += ", ";
		}
		arg_string += arg_info.name;
		is_first_arg = false;
	}
	if (p_func->is_vararg()) {
		// `MethodInfo` does not support the rest parameter name.
		arg_string += (p_func->get_argument_count() == 0) ? "...args" : ", ...args";
	}

	print_line(vformat("Function %s(%s)", p_func->get_name(), arg_string));
#ifdef TOOLS_ENABLED
	p_func->disassemble(p_lines);
#endif
	print_line("");
	print_line("");
}

static void recursively_disassemble_functions(const Ref<GDScript> p_script, const Vector<String> &p_lines) {
	print_line(vformat("Class %s", p_script->get_fully_qualified_name()));
	print_line("");
	print_line("");

	const GDScriptFunction *implicit_initializer = p_script->get_implicit_initializer();
	if (implicit_initializer != nullptr) {
		disassemble_function(implicit_initializer, p_lines);
	}

	const GDScriptFunction *implicit_ready = p_script->get_implicit_ready();
	if (implicit_ready != nullptr) {
		disassemble_function(implicit_ready, p_lines);
	}

	const GDScriptFunction *static_initializer = p_script->get_static_initializer();
	if (static_initializer != nullptr) {
		disassemble_function(static_initializer, p_lines);
	}

	for (const KeyValue<GDScriptFunction *, GDScript::LambdaInfo> &E : p_script->get_lambda_info()) {
		disassemble_function(E.key, p_lines);
	}

	for (const KeyValue<StringName, GDScriptFunction *> &E : p_script->get_member_functions()) {
		disassemble_function(E.value, p_lines);
	}

	for (const KeyValue<StringName, Ref<GDScript>> &E : p_script->get_subclasses()) {
		recursively_disassemble_functions(E.value, p_lines);
	}
}

static void test_compiler(const String &p_code, const String &p_script_path, const Vector<String> &p_lines) {
	GDScriptParser parser;
	Error err = parser.parse(p_code, p_script_path, false);

	if (err != OK) {
		print_line("Error in parser:");
		const List<GDScriptParser::ParserError> &errors = parser.get_errors();
		for (const GDScriptParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
		return;
	}

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	if (err != OK) {
		print_line("Error in analyzer:");
		const List<GDScriptParser::ParserError> &errors = parser.get_errors();
		for (const GDScriptParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
		return;
	}

	GDScriptCompiler compiler;
	Ref<GDScript> script;
	script.instantiate();
	script->set_path(p_script_path);

	err = compiler.compile(&parser, script.ptr(), false);

	if (err) {
		print_line("Error in compiler:");
		print_line(vformat("%02d:%02d: %s", compiler.get_error_line(), compiler.get_error_column(), compiler.get_error()));
		return;
	}

	recursively_disassemble_functions(script, p_lines);
}

void test(TestType p_type) {
	List<String> cmdlargs = OS::get_singleton()->get_cmdline_args();

	if (cmdlargs.is_empty()) {
		return;
	}

	String test = cmdlargs.back()->get();
	if (!test.ends_with(".gd") && !test.ends_with(".gdc")) {
		print_line("This test expects a path to a GDScript file as its last parameter. Got: " + test);
		return;
	}

	Ref<FileAccess> fa = FileAccess::open(test, FileAccess::READ);
	ERR_FAIL_COND_MSG(fa.is_null(), "Could not open file: " + test);

	// Initialize the language for the test routine.
	init_language(fa->get_path_absolute().get_base_dir());

	// Load global classes.
	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	for (int i = 0; i < script_classes.size(); i++) {
		Dictionary c = script_classes[i];
		if (!c.has("class") || !c.has("language") || !c.has("path") || !c.has("base") || !c.has("is_abstract") || !c.has("is_tool")) {
			continue;
		}
		ScriptServer::add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"]);
	}

	Vector<uint8_t> buf;
	uint64_t flen = fa->get_length();
	buf.resize(flen + 1);
	fa->get_buffer(buf.ptrw(), flen);
	buf.write[flen] = 0;

	String code = String::utf8((const char *)&buf[0]);

	Vector<String> lines;
	int last = 0;
	for (int i = 0; i <= code.length(); i++) {
		if (code[i] == '\n' || code[i] == 0) {
			lines.push_back(code.substr(last, i - last));
			last = i + 1;
		}
	}

	switch (p_type) {
		case TEST_TOKENIZER:
			test_tokenizer(code, lines);
			break;
		case TEST_TOKENIZER_BUFFER:
			if (test.ends_with(".gdc")) {
				test_tokenizer_buffer(buf, lines);
			} else {
				test_tokenizer_buffer(code, lines);
			}
			break;
		case TEST_PARSER:
			test_parser(code, test, lines);
			break;
		case TEST_COMPILER:
			test_compiler(code, test, lines);
			break;
		case TEST_BYTECODE:
			print_line("Not implemented.");
	}

	finish_language();
}
} // namespace GDScriptTests
