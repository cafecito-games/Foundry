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
#ifdef TOOLS_ENABLED
#include "../editor/gdscript_highlighter.h"
#endif
#include "../gdscript.h"
#include "../gdscript_analyzer.h"
#include "../gdscript_compiler.h"
#include "../gdscript_parser.h"
#include "../gdscript_reflection.h"
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
#include "scene/gui/text_edit.h"
#endif

namespace GDScriptTests {

TEST_CASE("[Modules][GDScript] Language reserved words include namespace declarations") {
	Vector<String> reserved_words = GDScriptLanguage::get_singleton()->get_reserved_words();
	CHECK(reserved_words.has("import"));
	CHECK(reserved_words.has("namespace"));
}

TEST_CASE("[Modules][GDScript] Language reserved words include hard trait declarations") {
	Vector<String> reserved_words = GDScriptLanguage::get_singleton()->get_reserved_words();
	CHECK(reserved_words.has("trait"));
	CHECK(reserved_words.has("trait_name"));
	CHECK_FALSE(reserved_words.has("uses"));
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

static MethodInfo find_method_info(const List<MethodInfo> &p_methods, const StringName &p_name) {
	for (const MethodInfo &method : p_methods) {
		if (method.name == p_name) {
			return method;
		}
	}
	return MethodInfo();
}

static Vector<StringName> get_script_trait_vector(const Ref<Script> &p_script) {
	List<StringName> trait_list;
	p_script->get_script_trait_list(&trait_list);

	Vector<StringName> traits;
	for (const StringName &trait : trait_list) {
		traits.push_back(trait);
	}
	return traits;
}

class TestGDScriptTraitReflectionAccessor {
public:
	static void set_base(const Ref<GDScript> &p_script, const Ref<GDScript> &p_base) {
		p_script->base = p_base;
	}

	static void set_script_trait_list(const Ref<GDScript> &p_script, const Vector<StringName> &p_traits) {
		p_script->script_trait_list = p_traits;
	}
};

class TestGDScriptGenericReflectionAccessor {
public:
	static void set_type_parameters(const Ref<GDScript> &p_script, const Vector<GDScript::TypeParameter> &p_parameters) {
		p_script->type_parameters = p_parameters;
	}
};

struct ScopedGDScriptNativeGlobals {
	bool initialized = false;

	ScopedGDScriptNativeGlobals() {
		if (!GDScriptLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			GDScriptLanguage::get_singleton()->init();
			initialized = true;
		}
	}

	~ScopedGDScriptNativeGlobals() {
		// Some sibling GDScript doctests initialize the language without finishing it; only tear down state owned by this guard.
		if (initialized) {
			GDScriptLanguage::get_singleton()->finish();
		}
	}
};

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

static const GDScriptParser::ClassNode *find_parser_trait(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type != GDScriptParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
				!member.m_class->is_trait || member.m_class->identifier == nullptr) {
			continue;
		}
		if (member.m_class->identifier->name == p_name) {
			return member.m_class;
		}
	}

	return nullptr;
}

static bool has_parser_error(const GDScriptParser &p_parser, const String &p_expected_error) {
	for (const GDScriptParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message == p_expected_error) {
			return true;
		}
	}
	return false;
}

static String first_parser_error_message(const GDScriptParser &p_parser) {
	for (const GDScriptParser::ParserError &parser_error : p_parser.get_errors()) {
		return parser_error.message;
	}
	return String();
}

static int count_parser_errors(const GDScriptParser &p_parser, const String &p_expected_error) {
	int count = 0;
	for (const GDScriptParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message == p_expected_error) {
			count++;
		}
	}
	return count;
}

static Error analyze_source(GDScriptParser &r_parser, const String &p_source,
		const String &p_path = "user://trait_analyzer_test.gd") {
	Error err = r_parser.parse(p_source, p_path, false);
	if (err != OK) {
		return err;
	}

	GDScriptAnalyzer analyzer(&r_parser);
	return analyzer.analyze();
}

#ifdef DEBUG_ENABLED
static bool has_parser_warning(const GDScriptParser &p_parser, GDScriptWarning::Code p_code, const String &p_expected_message) {
	for (const GDScriptWarning &warning : p_parser.get_warnings()) {
		if (warning.code == p_code && warning.get_message() == p_expected_message) {
			return true;
		}
	}
	return false;
}

static int count_parser_warnings(const GDScriptParser &p_parser, GDScriptWarning::Code p_code) {
	int count = 0;
	for (const GDScriptWarning &warning : p_parser.get_warnings()) {
		if (warning.code == p_code) {
			count++;
		}
	}
	return count;
}
#endif // DEBUG_ENABLED

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
		const bool is_trait = c.has("is_trait") && c["is_trait"];
		ScriptServer::add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"], is_trait);
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

static String register_global_script_class(const TempScriptFile &p_script) {
	String base_type;
	bool is_abstract = false;
	bool is_tool = false;
	bool is_trait = false;
	String class_name = GDScriptLanguage::get_singleton()->get_global_class_name(p_script.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait);
	CHECK_FALSE(class_name.is_empty());
	if (!class_name.is_empty()) {
		ScriptServer::add_global_class(class_name, base_type, GDScriptLanguage::get_singleton()->get_name(), p_script.path,
				is_abstract, is_tool, is_trait);
	}
	return class_name;
}

#ifdef TOOLS_ENABLED
static Color get_highlighted_color_at(const Dictionary &p_highlighting, int p_column) {
	Color color;
	Array columns = p_highlighting.keys();
	columns.sort();

	for (int i = 0; i < columns.size(); i++) {
		const int column = columns[i];
		if (column > p_column) {
			break;
		}
		const Dictionary info = p_highlighting[column];
		if (info.has("color")) {
			color = info["color"];
		}
	}

	return color;
}

TEST_CASE("[Modules][GDScript][Editor] Syntax highlighter treats async as contextual function modifier") {
	TextEdit *text_edit = memnew(TextEdit);
	text_edit->set_text(R"(async func load() -> void:
static async func make() -> void:
var async = 1
func async() -> int:
	return async
)");

	Ref<GDScriptSyntaxHighlighter> highlighter;
	highlighter.instantiate();
	highlighter->set_text_edit(text_edit);
	highlighter->_update_cache();

	const Color keyword_color = EDITOR_GET("text_editor/theme/highlighting/keyword_color");
	const Color normal_color = text_edit->get_theme_color(SceneStringName(font_color));
	const Color function_definition_color = EDITOR_GET("text_editor/theme/highlighting/gdscript/function_definition_color");

	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(0), 0), keyword_color);
	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(1), 7), keyword_color);
	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(2), 4), normal_color);
	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(3), 5), function_definition_color);

	memdelete(text_edit);
}
#endif // TOOLS_ENABLED

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

TEST_CASE("[Modules][GDScript] Parser stores global trait declarations and uses") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace characters.stats
trait_name Damageable
extends Node
uses Trackable, shared.CombatTag

signal died
var health: int = 100
)",
			"user://damageable.gd", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->is_trait);
	CHECK(root->trait_name_used);
	CHECK(root->identifier != nullptr);
	if (root->identifier == nullptr) {
		return;
	}
	CHECK_EQ(root->identifier->name, SNAME("Damageable"));
	CHECK_EQ(root->namespace_name, "characters.stats");
	CHECK_EQ(root->qualified_global_name, "characters.stats.Damageable");
	CHECK_EQ(root->fqcn, "characters.stats.Damageable");
	CHECK_EQ(root->extends.size(), 1);
	if (root->extends.size() != 1) {
		return;
	}
	CHECK_EQ(root->extends[0]->name, SNAME("Node"));
	CHECK_EQ(root->used_traits.size(), 2);
	if (root->used_traits.size() != 2) {
		return;
	}
	CHECK_EQ(root->used_traits[0].to_string(), "Trackable");
	CHECK_EQ(root->used_traits[1].to_string(), "shared.CombatTag");
}

TEST_CASE("[Modules][GDScript] Parser stores inline traits and class uses") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
class_name Player
extends Node
uses Damageable, characters.Movable

trait LocalTrait extends Node:
	uses Trackable
	func touch() -> void:
		pass
)",
			"user://player.gd", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK_FALSE(root->is_trait);
	CHECK_FALSE(root->trait_name_used);
	CHECK_EQ(root->used_traits.size(), 2);
	if (root->used_traits.size() != 2) {
		return;
	}
	CHECK_EQ(root->used_traits[0].to_string(), "Damageable");
	CHECK_EQ(root->used_traits[1].to_string(), "characters.Movable");

	const GDScriptParser::ClassNode *local_trait = find_parser_trait(root, SNAME("LocalTrait"));
	CHECK(local_trait != nullptr);
	if (local_trait == nullptr) {
		return;
	}
	CHECK(local_trait->is_trait);
	CHECK_FALSE(local_trait->trait_name_used);
	CHECK_EQ(local_trait->outer, root);
	CHECK_EQ(local_trait->fqcn, "Player::LocalTrait");
	CHECK_EQ(local_trait->extends.size(), 1);
	if (local_trait->extends.size() != 1) {
		return;
	}
	CHECK_EQ(local_trait->extends[0]->name, SNAME("Node"));
	CHECK_EQ(local_trait->used_traits.size(), 1);
	if (local_trait->used_traits.size() != 1) {
		return;
	}
	CHECK_EQ(local_trait->used_traits[0].to_string(), "Trackable");
	CHECK(find_parser_function(local_trait, SNAME("touch")) != nullptr);
}

TEST_CASE("[Modules][GDScript] Parser stores inline root trait metadata without extends") {
	const String script_path = "user://root_inline_trait.gd";
	GDScriptParser parser;
	Error err = parser.parse(R"(
trait LocalTrait uses Trackable:
	pass
)",
			script_path, false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const GDScriptParser::ClassNode *local_trait = find_parser_trait(root, SNAME("LocalTrait"));
	CHECK(local_trait != nullptr);
	if (local_trait == nullptr) {
		return;
	}
	CHECK_EQ(local_trait->outer, root);
	CHECK_EQ(local_trait->fqcn, GDScript::canonicalize_path(script_path) + "::LocalTrait");
	CHECK_EQ(local_trait->used_traits.size(), 1);
	if (local_trait->used_traits.size() != 1) {
		return;
	}
	CHECK_EQ(local_trait->used_traits[0].to_string(), "Trackable");
}

TEST_CASE("[Modules][GDScript] Parser keeps uses available as an identifier") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
var uses := 1

func echo(uses: int) -> int:
	var nested := uses
	return nested
)",
			"user://uses_identifier.gd", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("uses")));
	if (root->has_member(SNAME("uses"))) {
		CHECK_EQ(root->get_member(SNAME("uses")).type, GDScriptParser::ClassNode::Member::VARIABLE);
	}
	CHECK(find_parser_function(root, SNAME("echo")) != nullptr);
}

TEST_CASE("[Modules][GDScript] Analyzer resolves inline trait uses") {
	GDScriptParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
uses Damageable

trait Damageable:
	@abstract func take_damage(amount: int) -> void

func take_damage(amount: int) -> void:
	pass
)",
			"user://player_inline_trait_resolution.gd");

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	const GDScriptParser::ClassNode *damageable = find_parser_trait(root, SNAME("Damageable"));
	CHECK(damageable != nullptr);
	CHECK_EQ(root->used_traits.size(), 1);
	if (damageable == nullptr || root->used_traits.size() != 1) {
		return;
	}
	CHECK_EQ(root->used_traits[0].resolved_trait, damageable);
	CHECK_EQ(root->resolved_traits.size(), 1);
	CHECK_EQ(root->resolved_traits[0], damageable);
}

TEST_CASE("[Modules][GDScript] Analyzer resolves global and namespace-qualified trait uses") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile trait("namespaced_damageable_trait.gd", R"(
namespace characters
trait_name Damageable
extends RefCounted

@abstract func take_damage(amount: int) -> void
)");

	String base_type;
	bool is_abstract = true;
	bool is_tool = true;
	bool is_trait = false;
	String trait_name = GDScriptLanguage::get_singleton()->get_global_class_name(trait.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait);
	CHECK_EQ(trait_name, "characters.Damageable");
	CHECK_EQ(base_type, "RefCounted");
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
	CHECK(is_trait);
	if (trait_name.is_empty()) {
		return;
	}

	ScriptServer::add_global_class(trait_name, base_type, GDScriptLanguage::get_singleton()->get_name(), trait.path,
			is_abstract, is_tool, is_trait);

	GDScriptParser imported_parser;
	Error err = analyze_source(imported_parser, R"(
import characters
class_name ImportedPlayer
uses Damageable

func take_damage(amount: int) -> void:
	pass
)",
			"user://player_imported_trait_resolution.gd");

	INFO(first_parser_error_message(imported_parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *imported_root = imported_parser.get_tree();
	CHECK_EQ(imported_root->used_traits.size(), 1);
	if (imported_root->used_traits.size() != 1) {
		return;
	}
	CHECK(imported_root->used_traits[0].resolved_trait != nullptr);
	if (imported_root->used_traits[0].resolved_trait == nullptr) {
		return;
	}
	CHECK_EQ(imported_root->used_traits[0].resolved_trait->get_global_name(), SNAME("characters.Damageable"));

	GDScriptParser qualified_parser;
	err = analyze_source(qualified_parser, R"(
class_name QualifiedPlayer
uses characters.Damageable

func take_damage(amount: int) -> void:
	pass
)",
			"user://player_qualified_trait_resolution.gd");

	INFO(first_parser_error_message(qualified_parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *qualified_root = qualified_parser.get_tree();
	CHECK_EQ(qualified_root->used_traits.size(), 1);
	if (qualified_root->used_traits.size() != 1) {
		return;
	}
	CHECK(qualified_root->used_traits[0].resolved_trait != nullptr);
	if (qualified_root->used_traits[0].resolved_trait == nullptr) {
		return;
	}
	CHECK_EQ(qualified_root->used_traits[0].resolved_trait->get_global_name(), SNAME("characters.Damageable"));
}

TEST_CASE("[Modules][GDScript] Analyzer enforces trait base class constraints") {
	GDScriptParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
extends RefCounted
uses Movable

trait Movable extends Node2D:
	pass
)",
			"user://trait_base_constraint_error.gd");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(parser,
			R"(Class "Player" cannot use trait "Movable" because it does not inherit from "Node2D".)"));

	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile base_script("trait_base_constraint_external_base.gd", R"(
class_name ActorBase
extends Node2D
)");
	String base_class_name = register_global_script_class(base_script);
	CHECK_EQ(base_class_name, "ActorBase");

	GDScriptParser external_base_parser;
	err = analyze_source(external_base_parser, R"(
class_name Actor
extends ActorBase
uses Movable

trait Movable extends Node2D:
	pass
)",
			"user://trait_base_constraint_external_base_user.gd");

	INFO(first_parser_error_message(external_base_parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][GDScript] Analyzer enforces required trait methods") {
	GDScriptParser missing_parser;
	Error err = analyze_source(missing_parser, R"(
class_name Player
uses Damageable

trait Damageable:
	@abstract func take_damage(amount: int) -> void
)",
			"user://trait_required_method_error.gd");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(missing_parser,
			R"msg(Class "Player" must implement trait method "Damageable.take_damage()".)msg"));

	GDScriptParser async_parser;
	err = analyze_source(async_parser, R"(
class_name Player
uses RemoteLoadable

trait RemoteLoadable:
	@abstract async func fetch() -> String

func fetch() -> String:
	return ""
)",
			"user://trait_async_required_method_error.gd");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(async_parser,
			R"msg(The function "fetch()" must be async because it implements async trait method "RemoteLoadable.fetch()".)msg"));

	GDScriptParser sync_parser;
	err = analyze_source(sync_parser, R"(
class_name Player
uses LocalLoadable

trait LocalLoadable:
	@abstract func fetch() -> String

async func fetch() -> String:
	return ""
)",
			"user://trait_sync_required_method_error.gd");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(sync_parser,
			R"msg(The function "fetch()" cannot be async because it implements synchronous trait method )msg"
			R"msg("LocalLoadable.fetch()".)msg"));

	GDScriptParser signature_parser;
	err = analyze_source(signature_parser, R"(
class_name Player
uses Damageable

trait Damageable:
	@abstract func take_damage(amount: int) -> void

func take_damage(amount: String) -> void:
	pass
)",
			"user://trait_signature_required_method_error.gd");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(signature_parser,
			R"msg(The function "take_damage()" signature does not match required trait method "Damageable.take_damage()".)msg"));

	GDScriptParser native_parser;
	err = analyze_source(native_parser, R"(
class_name Named
extends RefCounted
uses NamedTrait

trait NamedTrait:
	@abstract func get_class() -> String
)",
			"user://trait_required_method_native_base.gd");

	INFO(first_parser_error_message(native_parser));
	CHECK_EQ(err, OK);

	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile base_script("trait_required_method_external_base.gd", R"(
class_name TraitMethodBase
extends RefCounted

func take_damage(amount: int) -> void:
	pass
)");
	String base_class_name = register_global_script_class(base_script);
	CHECK_EQ(base_class_name, "TraitMethodBase");

	GDScriptParser external_parser;
	err = analyze_source(external_parser, R"(
class_name Player
extends TraitMethodBase
uses Damageable

trait Damageable:
	@abstract func take_damage(amount: int) -> void
)",
			"user://trait_required_method_external_base_user.gd");

	INFO(first_parser_error_message(external_parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects trait inheritance and construction") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile trait("global_trait_inheritance_gate.gd", R"(
trait_name Damageable
)");
	String trait_class_name = register_global_script_class(trait);
	CHECK_EQ(trait_class_name, "Damageable");

	GDScriptParser extends_parser;
	Error err = analyze_source(extends_parser, R"(
class_name Player
extends Damageable
)",
			"user://trait_extends_error.gd");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(extends_parser,
			R"(Class "Player" cannot extend trait "Damageable"; use "uses Damageable" instead.)"));

	const String new_source = R"(
class_name Player

trait Damageable:
	pass

func test() -> void:
	var _damageable = Damageable.new()
)";
	TempScriptFile new_script("trait_constructor_error.gd", new_source);

	GDScriptParser new_parser;
	err = analyze_source(new_parser, new_source, new_script.path);

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(new_parser, R"(Cannot construct trait "Damageable".)"));
}

TEST_CASE("[Modules][GDScript] Analyzer accepts traits as static value types") {
	GDScriptParser annotation_parser;
	Error err = analyze_source(annotation_parser, R"(
class_name Player

trait Damageable:
	pass

var _damageable: Damageable
)",
			"user://trait_type_annotation.gd");

	INFO(first_parser_error_message(annotation_parser));
	CHECK_EQ(err, OK);

	GDScriptParser type_test_parser;
	err = analyze_source(type_test_parser, R"(
class_name Player

trait Damageable:
	pass

func test(value: Variant) -> void:
	var _result = value is Damageable
)",
			"user://trait_type_test.gd");

	INFO(first_parser_error_message(type_test_parser));
	CHECK_EQ(err, OK);

	GDScriptParser cast_parser;
	err = analyze_source(cast_parser, R"(
class_name Player

trait Damageable:
	pass

func test(value: Variant) -> void:
	var _result = value as Damageable
)",
			"user://trait_cast.gd");

	INFO(first_parser_error_message(cast_parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][GDScript] Analyzer resolves trait signatures in trait scope") {
	const String source = R"(
class_name Player
uses Loader

trait Loader:
	class Payload:
		pass

	@abstract func load(value: Payload) -> Payload

func load(value: Loader.Payload) -> Loader.Payload:
	return value
)";
	TempScriptFile script("trait_signature_scope_resolution.gd", source);

	GDScriptParser parser;
	Error err = analyze_source(parser, source, script.path);

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][GDScript] Analyzer emits one unresolved trait error") {
	GDScriptParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
uses MissingTrait
)",
			"user://trait_unresolved_single_error.gd");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK_EQ(count_parser_errors(parser, R"(Could not resolve trait "MissingTrait".)"), 1);
}

TEST_CASE("[Modules][GDScript] Analyzer resolves trait composition without diamond duplication") {
	GDScriptParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
uses Damageable, Trackable

trait Identified:
	@abstract func id() -> int

trait Damageable uses Identified:
	pass

trait Trackable uses Identified:
	pass

func id() -> int:
	return 1
)",
			"user://trait_diamond_resolution.gd");

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK_EQ(root->resolved_traits.size(), 3);
	CHECK_EQ(root->resolved_traits[0]->identifier->name, SNAME("Damageable"));
	CHECK_EQ(root->resolved_traits[1]->identifier->name, SNAME("Identified"));
	CHECK_EQ(root->resolved_traits[2]->identifier->name, SNAME("Trackable"));
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

TEST_CASE("[Modules][GDScript] Compiled coroutine functions reflect async method flags") {
	ScopedGDScriptNativeGlobals native_globals;
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
			"user://async_reflection.gd", false);

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

	GDScriptCompiler compiler;
	Ref<GDScript> script;
	script.instantiate();
	script->set_path("user://async_reflection.gd");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const MethodInfo explicit_async_info = script->get_method_info(SNAME("explicit_async"));
	CHECK_EQ(explicit_async_info.name, SNAME("explicit_async"));
	CHECK((explicit_async_info.flags & METHOD_FLAG_ASYNC) != 0);
	CHECK_EQ(explicit_async_info.return_val.type, Variant::INT);

	const MethodInfo body_inferred_async_info = script->get_method_info(SNAME("body_inferred_async"));
	CHECK_EQ(body_inferred_async_info.name, SNAME("body_inferred_async"));
	CHECK((body_inferred_async_info.flags & METHOD_FLAG_ASYNC) != 0);
	CHECK_EQ(body_inferred_async_info.return_val.type, Variant::INT);

	const MethodInfo synchronous_info = script->get_method_info(SNAME("synchronous"));
	CHECK_EQ(synchronous_info.name, SNAME("synchronous"));
	CHECK_FALSE((synchronous_info.flags & METHOD_FLAG_ASYNC) != 0);
	CHECK_EQ(synchronous_info.return_val.type, Variant::INT);

	Object *obj = ClassDB::instantiate(script->get_native()->get_name());
	CHECK(obj != nullptr);
	if (obj == nullptr) {
		return;
	}

	Ref<RefCounted> obj_ref;
	if (obj->is_ref_counted()) {
		obj_ref = Ref<RefCounted>(Object::cast_to<RefCounted>(obj));
	}
	obj->set_script(script);

	List<MethodInfo> reflected_methods;
	obj->get_method_list(&reflected_methods);

	const MethodInfo reflected_explicit_async_info = find_method_info(reflected_methods, SNAME("explicit_async"));
	CHECK_EQ(reflected_explicit_async_info.name, SNAME("explicit_async"));
	CHECK((reflected_explicit_async_info.flags & METHOD_FLAG_ASYNC) != 0);

	const MethodInfo reflected_body_inferred_async_info = find_method_info(reflected_methods, SNAME("body_inferred_async"));
	CHECK_EQ(reflected_body_inferred_async_info.name, SNAME("body_inferred_async"));
	CHECK((reflected_body_inferred_async_info.flags & METHOD_FLAG_ASYNC) != 0);

	const MethodInfo reflected_synchronous_info = find_method_info(reflected_methods, SNAME("synchronous"));
	CHECK_EQ(reflected_synchronous_info.name, SNAME("synchronous"));
	CHECK_FALSE((reflected_synchronous_info.flags & METHOD_FLAG_ASYNC) != 0);

	if (obj_ref.is_null()) {
		memdelete(obj);
	}
}

TEST_CASE("[Modules][GDScript] Compiled abstract functions reflect required method contracts") {
	ScopedGDScriptNativeGlobals native_globals;
	GDScriptParser parser;
	Error err = parser.parse(R"(
@abstract
class_name RequiredMethodReflection
extends RefCounted

@abstract
func required_contract(amount: int, label: String = "default") -> bool

@abstract
func untyped_required_contract()

func implemented() -> void:
	pass
)",
			"user://required_method_reflection.gd", false);

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

	GDScriptCompiler compiler;
	Ref<GDScript> script;
	script.instantiate();
	script->set_path("user://required_method_reflection.gd");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const MethodInfo required_info = script->get_method_info(SNAME("required_contract"));
	CHECK_EQ(required_info.name, SNAME("required_contract"));
	CHECK((required_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);
	CHECK_EQ(required_info.arguments.size(), 2);
	CHECK_EQ(required_info.arguments[0].type, Variant::INT);
	CHECK_EQ(required_info.arguments[1].type, Variant::STRING);
	CHECK_EQ(required_info.default_arguments.size(), 1);
	CHECK_EQ(required_info.return_val.type, Variant::BOOL);

	List<MethodInfo> script_methods;
	script->get_script_method_list(&script_methods);
	const MethodInfo reflected_required_info = find_method_info(script_methods, SNAME("required_contract"));
	CHECK_EQ(reflected_required_info.name, SNAME("required_contract"));
	CHECK((reflected_required_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);

	const MethodInfo untyped_required_info = script->get_method_info(SNAME("untyped_required_contract"));
	CHECK_EQ(untyped_required_info.name, SNAME("untyped_required_contract"));
	CHECK((untyped_required_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);
	CHECK_EQ(untyped_required_info.return_val.type, Variant::NIL);

	const MethodInfo implemented_info = script->get_method_info(SNAME("implemented"));
	CHECK_EQ(implemented_info.name, SNAME("implemented"));
	CHECK_FALSE((implemented_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);
}

TEST_CASE("[Modules][GDScript] Scripts reflect trait identities") {
	Ref<GDScript> base;
	base.instantiate();

	Ref<GDScript> script;
	script.instantiate();

	SUBCASE("empty by default") {
		Vector<StringName> reflected_traits = get_script_trait_vector(script);
		CHECK(reflected_traits.is_empty());

		Variant bound_traits_variant = script->call(SNAME("get_script_trait_list"));
		CHECK_EQ(bound_traits_variant.get_type(), Variant::ARRAY);

		TypedArray<StringName> bound_traits = bound_traits_variant;
		CHECK(bound_traits.is_empty());
		CHECK_FALSE(script->has_script_trait(SNAME("Damageable")));
		CHECK_FALSE(bool(script->call(SNAME("has_script_trait"), SNAME("Damageable"))));
	}

	SUBCASE("merges base traits with duplicate suppression") {
		Vector<StringName> base_traits;
		base_traits.push_back(SNAME("Damageable"));
		base_traits.push_back(SNAME("Trackable"));
		TestGDScriptTraitReflectionAccessor::set_script_trait_list(base, base_traits);

		Vector<StringName> script_traits;
		script_traits.push_back(SNAME("characters.Movable"));
		script_traits.push_back(SNAME("Damageable"));
		TestGDScriptTraitReflectionAccessor::set_script_trait_list(script, script_traits);
		TestGDScriptTraitReflectionAccessor::set_base(script, base);

		Vector<StringName> reflected_traits = get_script_trait_vector(script);
		CHECK_EQ(reflected_traits.size(), 3);
		CHECK_EQ(reflected_traits[0], SNAME("characters.Movable"));
		CHECK_EQ(reflected_traits[1], SNAME("Damageable"));
		CHECK_EQ(reflected_traits[2], SNAME("Trackable"));

		CHECK(script->has_script_trait(SNAME("characters.Movable")));
		CHECK(script->has_script_trait(SNAME("Damageable")));
		CHECK(script->has_script_trait(SNAME("Trackable")));
		CHECK_FALSE(script->has_script_trait(SNAME("MissingTrait")));

		Variant bound_traits_variant = script->call(SNAME("get_script_trait_list"));
		CHECK_EQ(bound_traits_variant.get_type(), Variant::ARRAY);

		TypedArray<StringName> bound_traits = bound_traits_variant;
		CHECK_EQ(bound_traits.size(), 3);
		const StringName first_bound_trait = bound_traits[0];
		const StringName second_bound_trait = bound_traits[1];
		const StringName third_bound_trait = bound_traits[2];
		CHECK_EQ(first_bound_trait, SNAME("characters.Movable"));
		CHECK_EQ(second_bound_trait, SNAME("Damageable"));
		CHECK_EQ(third_bound_trait, SNAME("Trackable"));

		CHECK(bool(script->call(SNAME("has_script_trait"), SNAME("Trackable"))));
		CHECK_FALSE(bool(script->call(SNAME("has_script_trait"), SNAME("MissingTrait"))));
	}
}

TEST_CASE("[Modules][GDScript] Scripts reflect declared generic type parameters") {
	ScopedGDScriptNativeGlobals native_globals;

	SUBCASE("non-generic script reports no parameters") {
		Ref<GDScript> script;
		script.instantiate();

		CHECK_FALSE(script->is_generic());
		CHECK(script->get_type_parameters().is_empty());

		Variant bound_variant = script->call(SNAME("get_type_parameter_list"));
		CHECK_EQ(bound_variant.get_type(), Variant::ARRAY);
		TypedArray<GDScriptTypeParameter> bound_parameters = bound_variant;
		CHECK(bound_parameters.is_empty());
		CHECK_FALSE(bool(script->call(SNAME("is_generic"))));
	}

	SUBCASE("reflection accessor exposes parameter descriptors") {
		Ref<GDScript> script;
		script.instantiate();

		Vector<GDScript::TypeParameter> parameters;

		GDScript::TypeParameter key;
		key.name = SNAME("K");
		key.index = 0;
		parameters.push_back(key);

		GDScript::TypeParameter value;
		value.name = SNAME("V");
		value.index = 1;
		value.has_bound = true;
		value.bound = PropertyInfo(Variant::OBJECT, "", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE, "RefCounted");
		parameters.push_back(value);

		TestGDScriptGenericReflectionAccessor::set_type_parameters(script, parameters);

		CHECK(script->is_generic());
		CHECK_EQ(script->get_type_parameters().size(), 2);

		Variant bound_variant = script->call(SNAME("get_type_parameter_list"));
		CHECK_EQ(bound_variant.get_type(), Variant::ARRAY);
		TypedArray<GDScriptTypeParameter> bound_parameters = bound_variant;
		CHECK_EQ(bound_parameters.size(), 2);
		if (bound_parameters.size() != 2) {
			return;
		}

		Ref<GDScriptTypeParameter> key_entry = bound_parameters[0];
		CHECK(key_entry.is_valid());
		Ref<GDScriptTypeParameter> value_entry = bound_parameters[1];
		CHECK(value_entry.is_valid());
		if (key_entry.is_null() || value_entry.is_null()) {
			return;
		}

		CHECK_EQ(key_entry->get_parameter_name(), SNAME("K"));
		CHECK_EQ(key_entry->get_index(), 0);
		CHECK_EQ(key_entry->get_scope(), SNAME("class"));
		CHECK_FALSE(key_entry->is_bounded());
		CHECK(key_entry->get_bound().is_empty());

		CHECK_EQ(value_entry->get_parameter_name(), SNAME("V"));
		CHECK_EQ(value_entry->get_index(), 1);
		CHECK(value_entry->is_bounded());
		const Dictionary bound_info = value_entry->get_bound();
		CHECK_EQ(int(bound_info["type"]), int(Variant::OBJECT));
		CHECK_EQ(StringName(bound_info["class_name"]), SNAME("RefCounted"));

		// The descriptors are also accessible through the bound property surface.
		CHECK_EQ(StringName(key_entry->get(SNAME("name"))), SNAME("K"));
		CHECK_EQ(int(value_entry->get(SNAME("index"))), 1);
		CHECK(bool(value_entry->get(SNAME("has_bound"))));
	}

	SUBCASE("compiled generic classes carry their parameters") {
		GDScriptParser parser;
		Error err = parser.parse(R"(
class_name GenericReflectionRoot[T]
extends RefCounted

class Pair[K, V: RefCounted]:
	var first
	var second
)",
				"user://generic_reflection.gd", false);
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

		GDScriptCompiler compiler;
		Ref<GDScript> script;
		script.instantiate();
		script->set_path("user://generic_reflection.gd");
		err = compiler.compile(&parser, script.ptr(), false);
		INFO(compiler.get_error());
		CHECK_EQ(err, OK);
		if (err != OK) {
			return;
		}

		CHECK(script->is_generic());
		const Vector<GDScript::TypeParameter> &root_parameters = script->get_type_parameters();
		CHECK_EQ(root_parameters.size(), 1);
		if (root_parameters.size() == 1) {
			CHECK_EQ(root_parameters[0].name, SNAME("T"));
			CHECK_EQ(root_parameters[0].index, 0);
			CHECK_FALSE(root_parameters[0].has_bound);
		}

		const Ref<GDScript> *pair_ptr = script->get_subclasses().getptr(SNAME("Pair"));
		CHECK(pair_ptr != nullptr);
		if (pair_ptr == nullptr) {
			return;
		}
		Ref<GDScript> pair = *pair_ptr;
		CHECK(pair.is_valid());
		if (pair.is_null()) {
			return;
		}
		const Vector<GDScript::TypeParameter> &pair_parameters = pair->get_type_parameters();
		CHECK_EQ(pair_parameters.size(), 2);
		if (pair_parameters.size() != 2) {
			return;
		}
		CHECK_EQ(pair_parameters[0].name, SNAME("K"));
		CHECK_EQ(pair_parameters[0].index, 0);
		CHECK_FALSE(pair_parameters[0].has_bound);
		CHECK_EQ(pair_parameters[1].name, SNAME("V"));
		CHECK_EQ(pair_parameters[1].index, 1);
		CHECK(pair_parameters[1].has_bound);
		CHECK_EQ(pair_parameters[1].bound.type, Variant::OBJECT);
		CHECK_EQ(pair_parameters[1].bound.class_name, SNAME("RefCounted"));
	}
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

TEST_CASE("[Modules][GDScript] Parser rejects invalid trait declaration ordering") {
	check_parse_source_error(R"(
class_name Actor
trait_name Damageable
)",
			R"("trait_name" cannot be combined with "class_name" in the same file.)");

	check_parse_source_error(R"(
trait_name Damageable
class_name Actor
)",
			R"("class_name" cannot be combined with "trait_name" in the same file.)");

	check_parse_source_error(R"(
func body() -> void:
	pass
uses Damageable
)",
			R"("uses" declarations must appear before class body declarations.)");

	check_parse_source_error(R"(
class Broken:
	func body() -> void:
		pass
	uses Damageable
)",
			R"("uses" declarations must appear before class body declarations.)");

	check_parse_source_error(R"(
class_name Actor
uses Damageable
uses Trackable
)",
			R"(Cannot use "uses" more than once in the same class.)");

	check_parse_source_error(R"(
class Broken:
	uses Damageable
	uses Trackable
)",
			R"(Cannot use "uses" more than once in the same class.)");

	check_parse_source_error(R"(
class Broken:
	uses Damageable
	extends Node
)",
			R"("extends" must appear before "uses".)");
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
	String class_name = GDScriptLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr,
			&is_abstract, &is_tool);

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

TEST_CASE("[Modules][GDScript] Global trait names use namespace-qualified identity") {
	TempScriptFile script("global_trait_name.gd", R"(
namespace characters
trait_name Damageable
extends Node2D
)");

	String base_type;
	bool is_abstract = true;
	bool is_tool = true;
	String class_name = GDScriptLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr, &is_abstract, &is_tool);

	CHECK_EQ(class_name, "characters.Damageable");
	CHECK_EQ(base_type, "Node2D");
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
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
			GDScriptLanguage::get_singleton()->get_name(), "user://qualified_runtime_global_name.gd", false, false, false);

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

TEST_CASE("[Modules][GDScript] Docgen emits async method qualifiers") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
async func load() -> int:
	return 1

static async func make() -> void:
	pass

func legacy_wait(done: Signal) -> void:
	await done
)",
			"user://async_docgen.gd", false);
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
	if (docs.size() != 1) {
		return;
	}

	String load_qualifiers;
	String make_qualifiers;
	String legacy_wait_qualifiers;
	for (const DocData::MethodDoc &method : docs[0].methods) {
		if (method.name == "load") {
			load_qualifiers = method.qualifiers;
		} else if (method.name == "make") {
			make_qualifiers = method.qualifiers;
		} else if (method.name == "legacy_wait") {
			legacy_wait_qualifiers = method.qualifiers;
		}
	}

	CHECK_EQ(load_qualifiers, "async");
	CHECK_EQ(make_qualifiers, "static async");
	CHECK_EQ(legacy_wait_qualifiers, "async");
}

TEST_CASE("[Modules][GDScript] Docgen marks traits") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
trait InnerTrait:
	func helper() -> void:
		pass

class PlainInner:
	pass
)",
			"user://trait_docgen.gd", false);
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

	bool found_trait = false;
	bool found_plain_inner = false;
	bool found_root = false;
	for (const DocData::ClassDoc &class_doc : docs) {
		if (class_doc.name.ends_with("InnerTrait")) {
			found_trait = true;
			CHECK(class_doc.is_trait);
		} else if (class_doc.name.ends_with("PlainInner")) {
			found_plain_inner = true;
			CHECK_FALSE(class_doc.is_trait);
		} else {
			found_root = true;
			CHECK_FALSE(class_doc.is_trait);
		}
	}
	CHECK(found_trait);
	CHECK(found_plain_inner);
	CHECK(found_root);

	// The trait marker survives a dictionary round-trip.
	DocData::ClassDoc trait_doc;
	trait_doc.is_trait = true;
	const DocData::ClassDoc restored = DocData::ClassDoc::from_dict(DocData::ClassDoc::to_dict(trait_doc));
	CHECK(restored.is_trait);
}

TEST_CASE("[Modules][GDScript] Docgen records trait uses") {
	GDScriptParser parser;
	Error err = parser.parse(R"(
trait Identified:
	func id() -> int:
		return 1

trait Damageable uses Identified:
	func describe() -> String:
		return "damage"

class Player:
	uses Damageable
)",
			"user://trait_uses_docgen.gd", false);
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

	DocData::ClassDoc damageable_doc;
	DocData::ClassDoc player_doc;
	for (const DocData::ClassDoc &class_doc : docs) {
		if (class_doc.name.ends_with(".Damageable")) {
			damageable_doc = class_doc;
		} else if (class_doc.name.ends_with(".Player")) {
			player_doc = class_doc;
		}
	}

	CHECK(!damageable_doc.name.is_empty());
	CHECK(!player_doc.name.is_empty());
	if (damageable_doc.name.is_empty() || player_doc.name.is_empty()) {
		return;
	}
	CHECK_EQ(damageable_doc.used_traits.size(), 1);
	if (damageable_doc.used_traits.is_empty()) {
		return;
	}
	CHECK(damageable_doc.used_traits[0].ends_with(".Identified"));
	CHECK_EQ(player_doc.used_traits.size(), 1);
	if (player_doc.used_traits.is_empty()) {
		return;
	}
	CHECK(player_doc.used_traits[0].ends_with(".Damageable"));

	const DocData::ClassDoc restored = DocData::ClassDoc::from_dict(DocData::ClassDoc::to_dict(player_doc));
	CHECK_EQ(restored.used_traits.size(), 1);
	if (restored.used_traits.is_empty()) {
		return;
	}
	CHECK(restored.used_traits[0].ends_with(".Damageable"));
}

TEST_CASE("[Modules][GDScript] Namespaced global classes can share a local name") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.Controller", "Node", GDScriptLanguage::get_singleton()->get_name(), "res://characters/controller.gd", false, false, false);
	ScriptServer::add_global_class("ui.Controller", "Node", GDScriptLanguage::get_singleton()->get_name(), "res://ui/controller.gd", false, false, false);

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
			"res://characters/base_character.gd", true, false, false);
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
	// Caches written before the `is_trait` flag existed default to not-a-trait.
	CHECK_FALSE(ScriptServer::is_global_class_trait("LegacyCharacter"));
}

TEST_CASE("[Modules][GDScript] Global class registry tracks the trait flag") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("combat.Damageable", "RefCounted", GDScriptLanguage::get_singleton()->get_name(),
			"res://combat/damageable.gd", false, false, true);
	ScriptServer::add_global_class("characters.BaseCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/base_character.gd", false, false, false);

	CHECK(ScriptServer::is_global_class_trait("combat.Damageable"));
	CHECK_FALSE(ScriptServer::is_global_class_trait("characters.BaseCharacter"));

	// Re-registering with a different flag updates the cached bit.
	ScriptServer::add_global_class("characters.BaseCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/base_character.gd", false, false, true);
	CHECK(ScriptServer::is_global_class_trait("characters.BaseCharacter"));
}

TEST_CASE("[Modules][GDScript] Global script class cache round-trips the trait flag") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->store_global_class_list(Array());

	ScriptServer::add_global_class("combat.Damageable", "RefCounted", GDScriptLanguage::get_singleton()->get_name(),
			"res://combat/damageable.gd", false, false, true);
	ScriptServer::save_global_classes();

	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	CHECK_EQ(script_classes.size(), 1);
	if (script_classes.size() == 1) {
		Dictionary script_class = script_classes[0];
		CHECK(script_class.has("is_trait"));
		CHECK(bool(script_class["is_trait"]));
	}

	// Reloading the serialized cache restores the trait flag.
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->refresh_global_class_list();
	CHECK(ScriptServer::is_global_class_trait("combat.Damageable"));
}

TEST_CASE("[Modules][GDScript] Fully qualified global class collisions keep both paths visible") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/base_character.gd", false, false, false);

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
			"res://characters/old_path.gd", false, false, false);
	ScriptServer::add_global_class("characters.MovedCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/new_path.gd", false, false, false);

	CHECK_EQ(ScriptServer::get_global_class_path("characters.MovedCharacter"), "res://characters/new_path.gd");
}

TEST_CASE("[Modules][GDScript] Global class cache version tracks mutations") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	const uint64_t empty_version = ScriptServer::get_global_class_cache_version();
	ScriptServer::add_global_class("characters.VersionedCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/versioned_character.gd", false, false, false);
	const uint64_t added_version = ScriptServer::get_global_class_cache_version();
	CHECK_NE(added_version, empty_version);

	ScriptServer::add_global_class("characters.VersionedCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/versioned_character.gd", false, false, false);
	CHECK_EQ(ScriptServer::get_global_class_cache_version(), added_version);

	ScriptServer::add_global_class("characters.VersionedCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(),
			"res://characters/renamed_character.gd", false, false, false);
	const uint64_t updated_version = ScriptServer::get_global_class_cache_version();
	CHECK_NE(updated_version, added_version);

	ScriptServer::remove_global_class("characters.VersionedCharacter");
	const uint64_t removed_version = ScriptServer::get_global_class_cache_version();
	CHECK_NE(removed_version, updated_version);

	ScriptServer::remove_global_class("characters.VersionedCharacter");
	CHECK_EQ(ScriptServer::get_global_class_cache_version(), removed_version);
}

#ifdef DEBUG_ENABLED
TEST_CASE("[Modules][GDScript] Analyzer reports mixed namespace directories through warning levels") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	const String warning_setting = GDScriptWarning::get_setting_path_from_code(GDScriptWarning::MIXED_NAMESPACE_DIRECTORY);
	const Variant original_warning_setting = ProjectSettings::get_singleton()->get_setting(warning_setting);

	TempScriptFile global_script("mixed_global_script.gd", R"(
class_name MixedGlobalScript
extends Node
)");
	TempScriptFile namespaced_script("mixed_namespaced_script.gd", R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)");
	TempScriptFile ui_script("mixed_ui_script.gd", R"(
namespace ui
class_name MixedUiScript
extends Node
)");

	ScriptServer::add_global_class("MixedGlobalScript", "Node", GDScriptLanguage::get_singleton()->get_name(), global_script.path, false, false, false);
	ScriptServer::add_global_class("characters.MixedNamespacedScript", "Node", GDScriptLanguage::get_singleton()->get_name(), namespaced_script.path, false, false, false);
	ScriptServer::add_global_class("ui.MixedUiScript", "Node", GDScriptLanguage::get_singleton()->get_name(), ui_script.path, false, false, false);

	const String expected_directory = GDScript::canonicalize_path(namespaced_script.path).get_base_dir();
	const String expected_warning = vformat(R"(Directory "%s" contains global script classes from mixed namespaces: "<global>", "characters", and "ui".)", expected_directory);

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)GDScriptWarning::WARN);
	GDScriptParser::update_project_settings();

	GDScriptParser warn_parser;
	Error err = warn_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&warn_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(warn_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	GDScriptParser global_trigger_parser;
	err = global_trigger_parser.parse(R"(
class_name MixedGlobalScript
extends Node
)",
			global_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&global_trigger_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(global_trigger_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)GDScriptWarning::IGNORE);
	GDScriptParser::update_project_settings();
	GDScriptParser ignore_parser;
	err = ignore_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&ignore_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK_EQ(count_parser_warnings(ignore_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY), 0);
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)GDScriptWarning::ERROR);
	GDScriptParser::update_project_settings();
	GDScriptParser error_parser;
	err = error_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&error_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK(has_parser_error(error_parser, expected_warning + " (Warning treated as error.)"));
	}

	ScriptServer::global_classes_clear();
	ScriptServer::add_global_class("MixedGlobalScript", "Node", GDScriptLanguage::get_singleton()->get_name(), global_script.path, false, false, false);
	ScriptServer::add_global_class("characters.MixedNamespacedScript", "Node", GDScriptLanguage::get_singleton()->get_name(), namespaced_script.path, false, false, false);

	const String expected_two_namespace_warning = vformat(R"(Directory "%s" contains global script classes from mixed namespaces: "<global>" and "characters".)", expected_directory);
	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)GDScriptWarning::WARN);
	GDScriptParser::update_project_settings();
	GDScriptParser two_namespace_parser;
	err = two_namespace_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&two_namespace_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(two_namespace_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY, expected_two_namespace_warning));
	}

	ScriptServer::global_classes_clear();
	TempScriptFile same_namespace_peer("same_namespace_peer.gd", R"(
namespace characters
class_name SameNamespacePeer
extends Node
)");
	ScriptServer::add_global_class("characters.MixedNamespacedScript", "Node", GDScriptLanguage::get_singleton()->get_name(), namespaced_script.path, false, false, false);
	ScriptServer::add_global_class("characters.SameNamespacePeer", "Node", GDScriptLanguage::get_singleton()->get_name(), same_namespace_peer.path, false, false, false);

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)GDScriptWarning::WARN);
	GDScriptParser::update_project_settings();
	GDScriptParser same_namespace_parser;
	err = same_namespace_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&same_namespace_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK_EQ(count_parser_warnings(same_namespace_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY), 0);
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, original_warning_setting);
	GDScriptParser::update_project_settings();
}
#endif // DEBUG_ENABLED

TEST_CASE("[Modules][GDScript] Analyzer resolves namespaced global classes") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile base_character("base_character.gd", R"(
namespace characters
class_name BaseCharacter
extends Node

enum Role { HERO = 11 }
)");
	TempScriptFile controller("my_character_controller.gd", R"(
namespace characters.controllers
class_name MyCharacterController
extends Node

enum State { IDLE = 17 }
)");
	TempScriptFile stat_block("stat_block.gd", R"(
namespace shared
class_name StatBlock
extends Resource
)");
	TempScriptFile global_base_character("global_base_character.gd", R"(
class_name BaseCharacter
extends Resource
)");
	TempScriptFile global_stat_block("global_stat_block.gd", R"(
class_name StatBlock
extends Node
)");

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(), base_character.path, false, false, false);
	ScriptServer::add_global_class("characters.controllers.MyCharacterController", "Node", GDScriptLanguage::get_singleton()->get_name(), controller.path, false, false, false);
	ScriptServer::add_global_class("shared.StatBlock", "Resource", GDScriptLanguage::get_singleton()->get_name(), stat_block.path, false, false, false);
	ScriptServer::add_global_class("BaseCharacter", "Resource", GDScriptLanguage::get_singleton()->get_name(), global_base_character.path, false, false, false);
	ScriptServer::add_global_class("StatBlock", "Node", GDScriptLanguage::get_singleton()->get_name(), global_stat_block.path, false, false, false);

	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace characters
import characters
import shared
class_name Hero
extends Node

var same_namespace: BaseCharacter
var imported: StatBlock
var child_namespace: controllers.MyCharacterController
var fully_qualified: characters.BaseCharacter
var same_namespace_nested: BaseCharacter.Role = BaseCharacter.Role.HERO
var child_namespace_nested: controllers.MyCharacterController.State = controllers.MyCharacterController.State.IDLE
var fully_qualified_nested: characters.BaseCharacter.Role = characters.BaseCharacter.Role.HERO

const SAME_NAMESPACE_ROLE = BaseCharacter.Role.HERO
const CHILD_NAMESPACE_STATE = controllers.MyCharacterController.State.IDLE
const FULLY_QUALIFIED_ROLE = characters.BaseCharacter.Role.HERO

func make_instances() -> void:
	var same := BaseCharacter.new()
	var child := controllers.MyCharacterController.new()
	var qualified := characters.BaseCharacter.new()
)",
			"user://hero.gd", false);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr || root->members.size() < 4) {
		return;
	}

	CHECK_EQ(root->members[0].variable->get_datatype().to_property_info("same_namespace").class_name, "characters.BaseCharacter");
	CHECK_EQ(root->members[1].variable->get_datatype().to_property_info("imported").class_name, "shared.StatBlock");
	CHECK_EQ(root->members[2].variable->get_datatype().to_property_info("child_namespace").class_name, "characters.controllers.MyCharacterController");
	CHECK_EQ(root->members[3].variable->get_datatype().to_property_info("fully_qualified").class_name, "characters.BaseCharacter");
	CHECK_EQ(root->members[4].variable->get_datatype().kind, GDScriptParser::DataType::ENUM);
	CHECK_EQ(root->members[4].variable->get_datatype().native_type, "characters.BaseCharacter.Role");
	CHECK(root->members[4].variable->initializer->is_constant);
	CHECK_EQ(root->members[4].variable->initializer->reduced_value, Variant(11));
	CHECK_EQ(root->members[5].variable->get_datatype().kind, GDScriptParser::DataType::ENUM);
	CHECK_EQ(root->members[5].variable->get_datatype().native_type, "characters.controllers.MyCharacterController.State");
	CHECK(root->members[5].variable->initializer->is_constant);
	CHECK_EQ(root->members[5].variable->initializer->reduced_value, Variant(17));
	CHECK_EQ(root->members[6].variable->get_datatype().kind, GDScriptParser::DataType::ENUM);
	CHECK_EQ(root->members[6].variable->get_datatype().native_type, "characters.BaseCharacter.Role");
	CHECK(root->members[6].variable->initializer->is_constant);
	CHECK_EQ(root->members[6].variable->initializer->reduced_value, Variant(11));

	const GDScriptParser::ConstantNode *same_namespace_role = root->get_member(SNAME("SAME_NAMESPACE_ROLE")).constant;
	CHECK(same_namespace_role != nullptr);
	if (same_namespace_role != nullptr) {
		CHECK(same_namespace_role->initializer->is_constant);
		CHECK_EQ(same_namespace_role->initializer->reduced_value, Variant(11));
	}

	const GDScriptParser::ConstantNode *child_namespace_state = root->get_member(SNAME("CHILD_NAMESPACE_STATE")).constant;
	CHECK(child_namespace_state != nullptr);
	if (child_namespace_state != nullptr) {
		CHECK(child_namespace_state->initializer->is_constant);
		CHECK_EQ(child_namespace_state->initializer->reduced_value, Variant(17));
	}

	const GDScriptParser::ConstantNode *fully_qualified_role = root->get_member(SNAME("FULLY_QUALIFIED_ROLE")).constant;
	CHECK(fully_qualified_role != nullptr);
	if (fully_qualified_role != nullptr) {
		CHECK(fully_qualified_role->initializer->is_constant);
		CHECK_EQ(fully_qualified_role->initializer->reduced_value, Variant(11));
	}
}

TEST_CASE("[Modules][GDScript] Analyzer resolves namespaced global traits") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile damageable("ns_trait_damageable.gd", R"(
namespace combat
trait_name Damageable

var health: int = 100
)");
	TempScriptFile loggable("ns_trait_loggable.gd", R"(
namespace shared
trait_name Loggable

var log_count: int = 0
)");
	TempScriptFile trackable("ns_trait_trackable.gd", R"(
namespace combat.controllers
trait_name Trackable

var tracked: bool = false
)");

	ScriptServer::add_global_class("combat.Damageable", "RefCounted", GDScriptLanguage::get_singleton()->get_name(), damageable.path, false, false, false);
	ScriptServer::add_global_class("shared.Loggable", "RefCounted", GDScriptLanguage::get_singleton()->get_name(), loggable.path, false, false, false);
	ScriptServer::add_global_class("combat.controllers.Trackable", "RefCounted", GDScriptLanguage::get_singleton()->get_name(), trackable.path, false, false, false);

	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace combat
import shared
extends RefCounted
uses Damageable, Loggable, controllers.Trackable

var same_namespace: Damageable
var imported: Loggable
var child_namespace: controllers.Trackable
var fully_qualified: combat.Damageable
)",
			"user://ns_trait_consumer.gd", false);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	CHECK_EQ(root->used_traits.size(), 3);
	if (root->used_traits.size() == 3) {
		CHECK(root->used_traits[0].resolved_trait != nullptr);
		CHECK(root->used_traits[1].resolved_trait != nullptr);
		CHECK(root->used_traits[2].resolved_trait != nullptr);
		if (root->used_traits[0].resolved_trait != nullptr) {
			CHECK_EQ(root->used_traits[0].resolved_trait->get_global_name(), StringName("combat.Damageable"));
		}
		if (root->used_traits[1].resolved_trait != nullptr) {
			CHECK_EQ(root->used_traits[1].resolved_trait->get_global_name(), StringName("shared.Loggable"));
		}
		if (root->used_traits[2].resolved_trait != nullptr) {
			CHECK_EQ(root->used_traits[2].resolved_trait->get_global_name(), StringName("combat.controllers.Trackable"));
		}
	}

	const GDScriptParser::VariableNode *same_namespace = root->get_member(SNAME("same_namespace")).variable;
	CHECK(same_namespace != nullptr);
	if (same_namespace != nullptr) {
		CHECK_EQ(same_namespace->get_datatype().to_property_info("same_namespace").class_name, "combat.Damageable");
	}

	const GDScriptParser::VariableNode *imported = root->get_member(SNAME("imported")).variable;
	CHECK(imported != nullptr);
	if (imported != nullptr) {
		CHECK_EQ(imported->get_datatype().to_property_info("imported").class_name, "shared.Loggable");
	}

	const GDScriptParser::VariableNode *child_namespace = root->get_member(SNAME("child_namespace")).variable;
	CHECK(child_namespace != nullptr);
	if (child_namespace != nullptr) {
		CHECK_EQ(child_namespace->get_datatype().to_property_info("child_namespace").class_name, "combat.controllers.Trackable");
	}

	const GDScriptParser::VariableNode *fully_qualified = root->get_member(SNAME("fully_qualified")).variable;
	CHECK(fully_qualified != nullptr);
	if (fully_qualified != nullptr) {
		CHECK_EQ(fully_qualified->get_datatype().to_property_info("fully_qualified").class_name, "combat.Damageable");
	}
}

#ifdef DEBUG_ENABLED
TEST_CASE("[Modules][GDScript] Analyzer reports mixed namespace directories for trait declarations") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	const String warning_setting = GDScriptWarning::get_setting_path_from_code(GDScriptWarning::MIXED_NAMESPACE_DIRECTORY);
	const Variant original_warning_setting = ProjectSettings::get_singleton()->get_setting(warning_setting);

	TempScriptFile namespaced_class("mixed_trait_class.gd", R"(
namespace characters
class_name MixedTraitClass
extends Node
)");
	TempScriptFile namespaced_trait("mixed_trait_decl.gd", R"(
namespace combat
trait_name MixedDeclTrait
)");

	ScriptServer::add_global_class("characters.MixedTraitClass", "Node", GDScriptLanguage::get_singleton()->get_name(), namespaced_class.path, false, false, false);
	ScriptServer::add_global_class("combat.MixedDeclTrait", "RefCounted", GDScriptLanguage::get_singleton()->get_name(), namespaced_trait.path, false, false, true);

	const String expected_directory = GDScript::canonicalize_path(namespaced_trait.path).get_base_dir();
	const String expected_warning = vformat(R"(Directory "%s" contains global script classes from mixed namespaces: "characters" and "combat".)", expected_directory);

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)GDScriptWarning::WARN);
	GDScriptParser::update_project_settings();

	// A `trait_name` declaration is a global script class, so analyzing it must surface the mixed-namespace warning.
	GDScriptParser trait_parser;
	Error err = trait_parser.parse(R"(
namespace combat
trait_name MixedDeclTrait
)",
			namespaced_trait.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&trait_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(trait_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	// A namespaced class sharing the directory with a differently-namespaced trait is warned too.
	GDScriptParser class_parser;
	err = class_parser.parse(R"(
namespace characters
class_name MixedTraitClass
extends Node
)",
			namespaced_class.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&class_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(class_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	// A trait sharing a namespace with a peer class in the same directory must NOT warn.
	ScriptServer::global_classes_clear();
	TempScriptFile same_namespace_peer("mixed_same_ns_peer.gd", R"(
namespace combat
class_name MixedSameNamespacePeer
extends Node
)");
	ScriptServer::add_global_class("combat.MixedDeclTrait", "RefCounted", GDScriptLanguage::get_singleton()->get_name(), namespaced_trait.path, false, false, true);
	ScriptServer::add_global_class("combat.MixedSameNamespacePeer", "Node", GDScriptLanguage::get_singleton()->get_name(), same_namespace_peer.path, false, false, false);

	GDScriptParser same_namespace_parser;
	err = same_namespace_parser.parse(R"(
namespace combat
trait_name MixedDeclTrait
)",
			namespaced_trait.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&same_namespace_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK_EQ(count_parser_warnings(same_namespace_parser, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY), 0);
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, original_warning_setting);
	GDScriptParser::update_project_settings();
}
#endif // DEBUG_ENABLED

TEST_CASE("[Modules][GDScript] Analyzer keeps local and native names ahead of namespace imports") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile imported_base("imported_base_character.gd", R"(
namespace characters
class_name BaseCharacter
extends Node
)");
	TempScriptFile imported_node("imported_node.gd", R"(
namespace characters
class_name Node
extends Resource
)");
	TempScriptFile imported_controller("imported_controller.gd", R"(
namespace characters.controllers
class_name MyCharacterController
extends Resource
)");

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", GDScriptLanguage::get_singleton()->get_name(), imported_base.path, false, false, false);
	ScriptServer::add_global_class("characters.Node", "Resource", GDScriptLanguage::get_singleton()->get_name(), imported_node.path, false, false, false);
	ScriptServer::add_global_class("characters.controllers.MyCharacterController", "Resource", GDScriptLanguage::get_singleton()->get_name(), imported_controller.path, false, false, false);

	GDScriptParser parser;
	Error err = parser.parse(R"(
import characters
class_name Precedence
extends Node

class BaseCharacter:
	extends RefCounted

var native_node: Node
var local_class: BaseCharacter
var controllers := { "MyCharacterController": 1 }

func check_local_precedence() -> int:
	var controllers := { "MyCharacterController": 1 }
	var controller_value: int = controllers.MyCharacterController
	return controller_value

func check_member_precedence() -> int:
	var controller_value: int = controllers.MyCharacterController
	return controller_value
)",
			"user://precedence.gd", false);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr || root->members.size() < 3) {
		return;
	}

	const GDScriptParser::DataType native_node_type = root->get_member(SNAME("native_node")).variable->get_datatype();
	CHECK_EQ(native_node_type.kind, GDScriptParser::DataType::NATIVE);
	CHECK_EQ(native_node_type.native_type, SNAME("Node"));

	const GDScriptParser::DataType local_class_type = root->get_member(SNAME("local_class")).variable->get_datatype();
	CHECK_EQ(local_class_type.kind, GDScriptParser::DataType::CLASS);
	CHECK_EQ(local_class_type.class_type, root->get_member(SNAME("BaseCharacter")).m_class);
}

TEST_CASE("[Modules][GDScript] Analyzer reports namespace import errors") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile characters_controller("characters_controller.gd", R"(
namespace characters
class_name Controller
extends Node
)");
	TempScriptFile ui_controller("ui_controller.gd", R"(
namespace ui
class_name Controller
extends Node
)");

	ScriptServer::add_global_class("characters.Controller", "Node", GDScriptLanguage::get_singleton()->get_name(), characters_controller.path, false, false, false);
	ScriptServer::add_global_class("ui.Controller", "Node", GDScriptLanguage::get_singleton()->get_name(), ui_controller.path, false, false, false);

	GDScriptParser missing_import_parser;
	Error err = missing_import_parser.parse(R"(
import missing.tools
class_name MissingImport
extends Node
)",
			"user://missing_import.gd", false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&missing_import_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK(has_parser_error(missing_import_parser, R"(Could not find imported namespace "missing.tools".)"));
	}

	GDScriptParser duplicate_missing_import_parser;
	err = duplicate_missing_import_parser.parse(R"(
import missing.tools
import missing.tools
class_name DuplicateMissingImport
extends Node
)",
			"user://duplicate_missing_import.gd", false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&duplicate_missing_import_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK_EQ(count_parser_errors(duplicate_missing_import_parser, R"(Could not find imported namespace "missing.tools".)"), 1);
	}

	GDScriptParser ambiguous_parser;
	err = ambiguous_parser.parse(R"(
import characters
import ui
class_name AmbiguousImport
extends Node

var controller: Controller
)",
			"user://ambiguous_import.gd", false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&ambiguous_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK(has_parser_error(ambiguous_parser, R"(Could not resolve type "Controller": imported namespaces "characters" and "ui" are ambiguous.)"));
	}
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
		const bool is_trait = c.has("is_trait") && c["is_trait"];
		ScriptServer::add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"], is_trait);
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

static const Vector<GDScript::AnnotationUsage> *find_annotation_usages(const HashMap<StringName, Vector<GDScript::AnnotationUsage>> &p_table, const StringName &p_name) {
	const Vector<GDScript::AnnotationUsage> *usages = p_table.getptr(p_name);
	return usages;
}

TEST_CASE("[Modules][GDScript] Compiled scripts persist custom annotation metadata") {
	ScopedGDScriptNativeGlobals native_globals;
	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace cafecito.persist_demo

extends RefCounted
uses Mixin

annotation suite(name: String = "") targets CLASS
annotation tags(...names: String) targets CLASS, METHOD
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation cases(provider: String) targets METHOD
annotation fixture targets VARIABLE
annotation label(text: String) targets VARIABLE

trait Mixin:
	@fixture
	@label("from_trait")
	var helper: int

	@test
	@tags("trait")
	func trait_method() -> void:
		pass

@fixture
var world: int

@export var exported_value: int = 0

@test
@timeout(10.0)
@cases(provider = "crit_rows")
@tags("a", "b")
func crit_table() -> void:
	pass

@suite(name = "Combat")
@tags("gameplay")
class Inner:
	@test
	func inner_method() -> void:
		pass
)",
			"user://annotation_metadata.gd", false);

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

	GDScriptCompiler compiler;
	Ref<GDScript> script;
	script.instantiate();
	script->set_path("user://annotation_metadata.gd");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	// Root class carries no annotations of its own.
	CHECK(script->get_class_annotations().is_empty());

	// Root method annotations preserve source order and split positional/named arguments.
	{
		const Vector<GDScript::AnnotationUsage> *usages = find_annotation_usages(script->get_method_annotations(), SNAME("crit_table"));
		CHECK(usages != nullptr);
		if (usages != nullptr) {
			CHECK_EQ(usages->size(), 4);
			if (usages->size() == 4) {
				CHECK_EQ((*usages)[0].name, SNAME("test"));
				CHECK_EQ((*usages)[0].qualified_name, SNAME("cafecito.persist_demo.test"));
				CHECK((*usages)[0].args.is_empty());
				CHECK((*usages)[0].kwargs.is_empty());

				CHECK_EQ((*usages)[1].name, SNAME("timeout"));
				CHECK_EQ((*usages)[1].args.size(), 1);
				if (!(*usages)[1].args.is_empty()) {
					CHECK_EQ(double((*usages)[1].args[0]), doctest::Approx(10.0));
				}
				CHECK((*usages)[1].kwargs.is_empty());

				CHECK_EQ((*usages)[2].name, SNAME("cases"));
				CHECK((*usages)[2].args.is_empty());
				CHECK_EQ((*usages)[2].kwargs.size(), 1);
				CHECK_EQ(String((*usages)[2].kwargs[SNAME("provider")]), "crit_rows");

				CHECK_EQ((*usages)[3].name, SNAME("tags"));
				CHECK_EQ((*usages)[3].args.size(), 2);
				if ((*usages)[3].args.size() == 2) {
					CHECK_EQ(String((*usages)[3].args[0]), "a");
					CHECK_EQ(String((*usages)[3].args[1]), "b");
				}
			}
		}
	}

	// Root member-variable annotations. Custom usages carry `is_builtin == false`.
	{
		const Vector<GDScript::AnnotationUsage> *usages = find_annotation_usages(script->get_variable_annotations(), SNAME("world"));
		CHECK(usages != nullptr);
		if (usages != nullptr) {
			CHECK_EQ(usages->size(), 1);
			if (!usages->is_empty()) {
				CHECK_EQ((*usages)[0].name, SNAME("fixture"));
				CHECK_FALSE((*usages)[0].is_builtin);
			}
		}

		// Built-in annotations such as `@export` are now reflected as metadata, tagged `is_builtin`.
		const Vector<GDScript::AnnotationUsage> *exported = find_annotation_usages(script->get_variable_annotations(), SNAME("exported_value"));
		CHECK(exported != nullptr);
		if (exported != nullptr) {
			CHECK_EQ(exported->size(), 1);
			if (!exported->is_empty()) {
				CHECK_EQ((*exported)[0].name, SNAME("export"));
				CHECK_EQ((*exported)[0].qualified_name, SNAME("export"));
				CHECK((*exported)[0].is_builtin);
				CHECK((*exported)[0].args.is_empty());
				CHECK((*exported)[0].kwargs.is_empty());
			}
		}
	}

	// Concrete trait members flattened into the implementer carry their declaration annotations.
	{
		const Vector<GDScript::AnnotationUsage> *method_usages = find_annotation_usages(script->get_method_annotations(), SNAME("trait_method"));
		CHECK(method_usages != nullptr);
		if (method_usages != nullptr) {
			CHECK_EQ(method_usages->size(), 2);
			if (method_usages->size() == 2) {
				CHECK_EQ((*method_usages)[0].name, SNAME("test"));
				CHECK_EQ((*method_usages)[1].name, SNAME("tags"));
				CHECK_EQ((*method_usages)[1].args.size(), 1);
				if (!(*method_usages)[1].args.is_empty()) {
					CHECK_EQ(String((*method_usages)[1].args[0]), "trait");
				}
			}
		}

		const Vector<GDScript::AnnotationUsage> *variable_usages = find_annotation_usages(script->get_variable_annotations(), SNAME("helper"));
		CHECK(variable_usages != nullptr);
		if (variable_usages != nullptr) {
			CHECK_EQ(variable_usages->size(), 2);
			if (variable_usages->size() == 2) {
				CHECK_EQ((*variable_usages)[0].name, SNAME("fixture"));
				CHECK_EQ((*variable_usages)[1].name, SNAME("label"));
				CHECK_EQ((*variable_usages)[1].args.size(), 1);
				if (!(*variable_usages)[1].args.is_empty()) {
					CHECK_EQ(String((*variable_usages)[1].args[0]), "from_trait");
				}
			}
		}
	}

	// Inner-class annotations live on the compiled subclass and are direct-only.
	{
		const HashMap<StringName, Ref<GDScript>> &subclasses = script->get_subclasses();
		CHECK(subclasses.has(SNAME("Inner")));
		if (subclasses.has(SNAME("Inner"))) {
			Ref<GDScript> inner = subclasses[SNAME("Inner")];
			CHECK(inner.is_valid());
			if (inner.is_valid()) {
				const Vector<GDScript::AnnotationUsage> &class_usages = inner->get_class_annotations();
				CHECK_EQ(class_usages.size(), 2);
				if (class_usages.size() == 2) {
					CHECK_EQ(class_usages[0].name, SNAME("suite"));
					CHECK_EQ(class_usages[0].qualified_name, SNAME("cafecito.persist_demo.suite"));
					CHECK_EQ(class_usages[0].kwargs.size(), 1);
					CHECK_EQ(String(class_usages[0].kwargs[SNAME("name")]), "Combat");
					CHECK_EQ(class_usages[1].name, SNAME("tags"));
					CHECK_EQ(class_usages[1].args.size(), 1);
					if (!class_usages[1].args.is_empty()) {
						CHECK_EQ(String(class_usages[1].args[0]), "gameplay");
					}
				}

				const Vector<GDScript::AnnotationUsage> *inner_method = find_annotation_usages(inner->get_method_annotations(), SNAME("inner_method"));
				CHECK(inner_method != nullptr);
				if (inner_method != nullptr) {
					CHECK_EQ(inner_method->size(), 1);
					if (!inner_method->is_empty()) {
						CHECK_EQ((*inner_method)[0].name, SNAME("test"));
					}
				}
			}
		}
	}

	// Clearing the compiled script wipes the persisted annotation metadata.
	script->clear();
	CHECK(script->get_class_annotations().is_empty());
	CHECK(script->get_method_annotations().is_empty());
	CHECK(script->get_variable_annotations().is_empty());
}

TEST_CASE("[Modules][GDScript] Compiled scripts persist built-in annotation metadata") {
	ScopedGDScriptNativeGlobals native_globals;
	GDScriptParser parser;
	Error err = parser.parse(R"(
@tool
extends Node

annotation test targets METHOD

@export_range(0, 100) var ranged: int = 1

@onready var ready_node: Node = self

@export var exported: int = 0

@rpc("any_peer", "reliable")
func networked() -> void:
	pass

@test
func custom_and_builtin() -> void:
	pass
)",
			"user://builtin_annotation_metadata.gd", false);

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

	GDScriptCompiler compiler;
	Ref<GDScript> script;
	script.instantiate();
	script->set_path("user://builtin_annotation_metadata.gd");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	// Script-configuration annotations (`@tool`, `@icon`, `@static_unload`) are applied by the parser
	// and not retained on the AST, so they are intentionally not surfaced as class annotations.
	CHECK(script->get_class_annotations().is_empty());

	// `@export_range(0, 100)` carries its positional arguments in `args`.
	{
		const Vector<GDScript::AnnotationUsage> *usages = find_annotation_usages(script->get_variable_annotations(), SNAME("ranged"));
		CHECK(usages != nullptr);
		if (usages != nullptr) {
			CHECK_EQ(usages->size(), 1);
			if (!usages->is_empty()) {
				CHECK_EQ((*usages)[0].name, SNAME("export_range"));
				CHECK((*usages)[0].is_builtin);
				CHECK_EQ((*usages)[0].args.size(), 2);
				if ((*usages)[0].args.size() == 2) {
					CHECK_EQ(int((*usages)[0].args[0]), 0);
					CHECK_EQ(int((*usages)[0].args[1]), 100);
				}
				CHECK((*usages)[0].kwargs.is_empty());
			}
		}
	}

	// `@onready` is a marker built-in with no arguments.
	{
		const Vector<GDScript::AnnotationUsage> *usages = find_annotation_usages(script->get_variable_annotations(), SNAME("ready_node"));
		CHECK(usages != nullptr);
		if (usages != nullptr && !usages->is_empty()) {
			CHECK_EQ((*usages)[0].name, SNAME("onready"));
			CHECK((*usages)[0].is_builtin);
			CHECK((*usages)[0].args.is_empty());
		}
	}

	// `@rpc` arguments are reflected positionally.
	{
		const Vector<GDScript::AnnotationUsage> *usages = find_annotation_usages(script->get_method_annotations(), SNAME("networked"));
		CHECK(usages != nullptr);
		if (usages != nullptr && !usages->is_empty()) {
			CHECK_EQ((*usages)[0].name, SNAME("rpc"));
			CHECK((*usages)[0].is_builtin);
			CHECK_EQ((*usages)[0].args.size(), 2);
		}
	}

	// A custom annotation on a method is still recorded and stays non-built-in.
	{
		const Vector<GDScript::AnnotationUsage> *usages = find_annotation_usages(script->get_method_annotations(), SNAME("custom_and_builtin"));
		CHECK(usages != nullptr);
		if (usages != nullptr && !usages->is_empty()) {
			CHECK_EQ((*usages)[0].name, SNAME("test"));
			CHECK_FALSE((*usages)[0].is_builtin);
		}
	}
}

TEST_CASE("[Modules][GDScript] GDScriptAnnotation descriptor snapshots annotation metadata") {
	GDScript::AnnotationUsage usage;
	usage.name = SNAME("timeout");
	usage.qualified_name = SNAME("cafecito.test.timeout");
	usage.args.push_back(10.0);
	usage.args.push_back("slow");
	usage.kwargs[SNAME("provider")] = "crit_rows";

	Ref<GDScriptAnnotation> descriptor = GDScriptAnnotation::from_usage(usage);
	CHECK(descriptor.is_valid());
	if (descriptor.is_null()) {
		return;
	}

	SUBCASE("getters expose the resolved metadata") {
		CHECK_EQ(descriptor->get_annotation_name(), SNAME("timeout"));
		CHECK_EQ(descriptor->get_qualified_name(), SNAME("cafecito.test.timeout"));

		Array args = descriptor->get_arguments();
		CHECK_EQ(args.size(), 2);
		if (args.size() == 2) {
			CHECK_EQ(double(args[0]), 10.0);
			CHECK_EQ(String(args[1]), "slow");
		}

		Dictionary kwargs = descriptor->get_named_arguments();
		CHECK_EQ(kwargs.size(), 1);
		CHECK_EQ(String(kwargs[SNAME("provider")]), "crit_rows");

		// Custom annotations default to non-built-in.
		CHECK_FALSE(descriptor->is_builtin());
	}

	SUBCASE("bound accessors are reachable through the script API") {
		CHECK_EQ(descriptor->call(SNAME("get_annotation_name")), Variant(SNAME("timeout")));
		CHECK_EQ(descriptor->call(SNAME("get_qualified_name")), Variant(SNAME("cafecito.test.timeout")));
		CHECK_EQ(descriptor->get(SNAME("name")), Variant(SNAME("timeout")));
		CHECK_EQ(Array(descriptor->get(SNAME("args"))).size(), 2);
		CHECK_EQ(Dictionary(descriptor->get(SNAME("kwargs"))).size(), 1);
		CHECK_EQ(descriptor->get(SNAME("builtin")), Variant(false));
	}

	SUBCASE("the built-in flag is carried from the source usage") {
		GDScript::AnnotationUsage builtin_usage;
		builtin_usage.name = SNAME("export_range");
		builtin_usage.qualified_name = SNAME("export_range");
		builtin_usage.args.push_back(0.0);
		builtin_usage.args.push_back(100.0);
		builtin_usage.is_builtin = true;

		Ref<GDScriptAnnotation> builtin_descriptor = GDScriptAnnotation::from_usage(builtin_usage);
		CHECK(builtin_descriptor.is_valid());
		if (builtin_descriptor.is_valid()) {
			CHECK(builtin_descriptor->is_builtin());
			CHECK_EQ(builtin_descriptor->get(SNAME("builtin")), Variant(true));
			CHECK_EQ(builtin_descriptor->get_arguments().size(), 2);
		}
	}

	SUBCASE("mutating returned snapshots leaves the descriptor and source metadata intact") {
		Array args = descriptor->get_arguments();
		args.push_back("injected");
		Dictionary kwargs = descriptor->get_named_arguments();
		kwargs[SNAME("evil")] = true;

		// The next read is unaffected by the previous mutation.
		CHECK_EQ(descriptor->get_arguments().size(), 2);
		CHECK_EQ(descriptor->get_named_arguments().size(), 1);

		// The originating usage metadata is untouched.
		CHECK_EQ(usage.args.size(), 2);
		CHECK_EQ(usage.kwargs.size(), 1);
	}
}

static Dictionary find_descriptor_by_name(const TypedArray<Dictionary> &p_descriptors, const StringName &p_name) {
	for (int i = 0; i < p_descriptors.size(); i++) {
		const Dictionary descriptor = p_descriptors[i];
		if (StringName(descriptor.get("name", StringName())) == p_name) {
			return descriptor;
		}
	}
	return Dictionary();
}

TEST_CASE("[Modules][GDScript] GDScriptReflection exposes custom annotation metadata") {
	ScopedGDScriptNativeGlobals native_globals;
	GDScriptParser parser;
	Error err = parser.parse(R"(
namespace cafecito.reflect_cpp

annotation suite(name: String = "") targets CLASS
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation fixture targets VARIABLE
annotation tags(...names: String) targets METHOD
annotation repeatable(value: String) targets METHOD

@suite(name = "Base Suite")
class Base:
	@fixture
	var base_var: int = 0

	@test
	@timeout(2.0)
	func base_method() -> void:
		pass

	@timeout(7.0)
	func shared_method() -> void:
		pass

class Derived extends Base:
	@test
	func derived_method() -> void:
		pass

	@tags("override")
	func shared_method() -> void:
		pass

trait Mixin:
	@test
	@tags("trait")
	func mixin_method() -> void:
		pass

class Impl uses Mixin:
	@repeatable("a")
	@repeatable("b")
	func repeated() -> void:
		pass
)",
			"user://annotation_reflection_cpp.gd", false);

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

	GDScriptCompiler compiler;
	Ref<GDScript> script;
	script.instantiate();
	script->set_path("user://annotation_reflection_cpp.gd");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const HashMap<StringName, Ref<GDScript>> &subclasses = script->get_subclasses();
	CHECK(subclasses.has(SNAME("Base")));
	CHECK(subclasses.has(SNAME("Derived")));
	CHECK(subclasses.has(SNAME("Impl")));
	if (!subclasses.has(SNAME("Base")) || !subclasses.has(SNAME("Derived")) || !subclasses.has(SNAME("Impl"))) {
		return;
	}
	Ref<GDScript> base = subclasses[SNAME("Base")];
	Ref<GDScript> derived = subclasses[SNAME("Derived")];
	Ref<GDScript> impl = subclasses[SNAME("Impl")];

	Ref<GDScriptReflection> reflection;
	reflection.instantiate();

	SUBCASE("class annotations are direct-only") {
		TypedArray<GDScriptAnnotation> base_class = reflection->get_class_annotations(base);
		CHECK_EQ(base_class.size(), 1);
		if (base_class.size() == 1) {
			Ref<GDScriptAnnotation> suite = base_class[0];
			CHECK(suite.is_valid());
			if (suite.is_valid()) {
				CHECK_EQ(suite->get_annotation_name(), SNAME("suite"));
				CHECK_EQ(suite->get_qualified_name(), SNAME("cafecito.reflect_cpp.suite"));
				CHECK_EQ(String(suite->get_named_arguments()[SNAME("name")]), "Base Suite");
			}
		}

		// Derived defines no class annotations of its own and does not inherit them.
		CHECK(reflection->get_class_annotations(derived).is_empty());
	}

	SUBCASE("method annotations follow the effective view") {
		TypedArray<GDScriptAnnotation> base_method = reflection->get_method_annotations(base, SNAME("base_method"));
		CHECK_EQ(base_method.size(), 2);
		if (base_method.size() == 2) {
			CHECK_EQ(Ref<GDScriptAnnotation>(base_method[0])->get_annotation_name(), SNAME("test"));
			CHECK_EQ(Ref<GDScriptAnnotation>(base_method[1])->get_annotation_name(), SNAME("timeout"));
		}

		// Base method annotations remain visible through the derived script.
		CHECK_EQ(reflection->get_method_annotations(derived, SNAME("base_method")).size(), 2);

		// A trait-flattened method carries its declaration annotations through the implementer.
		TypedArray<GDScriptAnnotation> mixin_method = reflection->get_method_annotations(impl, SNAME("mixin_method"));
		CHECK_EQ(mixin_method.size(), 2);
		if (mixin_method.size() == 2) {
			CHECK_EQ(Ref<GDScriptAnnotation>(mixin_method[0])->get_annotation_name(), SNAME("test"));
			CHECK_EQ(Ref<GDScriptAnnotation>(mixin_method[1])->get_annotation_name(), SNAME("tags"));
		}
	}

	SUBCASE("variable annotations follow the effective view") {
		TypedArray<GDScriptAnnotation> base_var = reflection->get_variable_annotations(base, SNAME("base_var"));
		CHECK_EQ(base_var.size(), 1);
		if (base_var.size() == 1) {
			CHECK_EQ(Ref<GDScriptAnnotation>(base_var[0])->get_annotation_name(), SNAME("fixture"));
		}
		CHECK_EQ(reflection->get_variable_annotations(derived, SNAME("base_var")).size(), 1);
	}

	SUBCASE("repeated annotations are preserved in source order") {
		TypedArray<GDScriptAnnotation> repeated = reflection->get_method_annotations(impl, SNAME("repeated"));
		CHECK_EQ(repeated.size(), 2);
		if (repeated.size() == 2) {
			CHECK_EQ(String(Ref<GDScriptAnnotation>(repeated[0])->get_arguments()[0]), "a");
			CHECK_EQ(String(Ref<GDScriptAnnotation>(repeated[1])->get_arguments()[0]), "b");
		}
	}

	SUBCASE("has_annotation and get_annotation match short and qualified names") {
		CHECK(reflection->has_annotation(base, SNAME("base_method"), SNAME("test"), SNAME("method")));
		CHECK(reflection->has_annotation(base, SNAME("base_method"), SNAME("cafecito.reflect_cpp.timeout"), SNAME("method")));
		CHECK_FALSE(reflection->has_annotation(base, SNAME("base_method"), SNAME("missing"), SNAME("method")));
		CHECK(reflection->has_annotation(base, SNAME(""), SNAME("suite"), SNAME("class")));
		CHECK(reflection->has_annotation(base, SNAME("base_var"), SNAME("fixture"), SNAME("variable")));

		Ref<GDScriptAnnotation> timeout = reflection->get_annotation(base, SNAME("base_method"), SNAME("timeout"), SNAME("method"));
		CHECK(timeout.is_valid());
		if (timeout.is_valid()) {
			CHECK_EQ(double(timeout->get_arguments()[0]), doctest::Approx(2.0));
		}
		CHECK(reflection->get_annotation(base, SNAME("base_method"), SNAME("missing"), SNAME("method")).is_null());
	}

	SUBCASE("generic get_annotations dispatches on kind") {
		CHECK_EQ(reflection->get_annotations(base, SNAME(""), SNAME("class")).size(), 1);
		CHECK_EQ(reflection->get_annotations(base, SNAME("base_method"), SNAME("method")).size(), 2);
		CHECK_EQ(reflection->get_annotations(base, SNAME("base_var"), SNAME("variable")).size(), 1);
	}

	SUBCASE("descriptors embed annotations in method and property dictionaries") {
		const Dictionary method_descriptor = find_descriptor_by_name(reflection->get_methods(base), SNAME("base_method"));
		CHECK(method_descriptor.has("annotations"));
		CHECK_EQ(Array(method_descriptor["annotations"]).size(), 2);

		const Dictionary method_info = reflection->get_method_info(base, SNAME("base_method"));
		CHECK(method_info.has("annotations"));
		CHECK_EQ(Array(method_info["annotations"]).size(), 2);

		const Dictionary property_descriptor = find_descriptor_by_name(reflection->get_properties(base), SNAME("base_var"));
		CHECK(property_descriptor.has("annotations"));
		CHECK_EQ(Array(property_descriptor["annotations"]).size(), 1);
	}

	SUBCASE("an override and the base method it shadows keep their own embedded annotations") {
		// Effective method annotations resolve to the override.
		TypedArray<GDScriptAnnotation> effective = reflection->get_method_annotations(derived, SNAME("shared_method"));
		CHECK_EQ(effective.size(), 1);
		if (effective.size() == 1) {
			CHECK_EQ(Ref<GDScriptAnnotation>(effective[0])->get_annotation_name(), SNAME("tags"));
		}

		// get_methods(Derived) lists both shared_method declarations (override + base), each carrying
		// the annotations of the declaration it represents rather than the leaf's effective set.
		TypedArray<Dictionary> methods = reflection->get_methods(derived);
		int shared_entries = 0;
		bool saw_override_tags = false;
		bool saw_base_timeout = false;
		for (int i = 0; i < methods.size(); i++) {
			const Dictionary descriptor = methods[i];
			if (StringName(descriptor.get("name", StringName())) != SNAME("shared_method")) {
				continue;
			}
			shared_entries++;
			const TypedArray<GDScriptAnnotation> annotations = descriptor["annotations"];
			if (annotations.size() == 1) {
				const StringName annotation_name = Ref<GDScriptAnnotation>(annotations[0])->get_annotation_name();
				saw_override_tags = saw_override_tags || annotation_name == SNAME("tags");
				saw_base_timeout = saw_base_timeout || annotation_name == SNAME("timeout");
			}
		}
		CHECK_EQ(shared_entries, 2);
		CHECK(saw_override_tags);
		CHECK(saw_base_timeout);
	}

	SUBCASE("invalid, non-script, and freed targets return empty results without crashing") {
		CHECK(reflection->get_class_annotations(Variant(42)).is_empty());
		CHECK(reflection->get_method_annotations(Variant(), SNAME("base_method")).is_empty());
		CHECK(reflection->get_variable_annotations(Variant("not a script"), SNAME("base_var")).is_empty());
		CHECK_FALSE(reflection->has_annotation(Variant(42), SNAME("base_method"), SNAME("test"), SNAME("method")));
		CHECK(reflection->get_annotation(Variant(42), SNAME("base_method"), SNAME("test"), SNAME("method")).is_null());

		Object *freed = memnew(Object);
		Variant freed_target(freed);
		memdelete(freed);
		CHECK(reflection->get_class_annotations(freed_target).is_empty());
		CHECK_FALSE(reflection->has_annotation(freed_target, SNAME("base_method"), SNAME("test"), SNAME("method")));
		CHECK(reflection->get_annotation(freed_target, SNAME("base_method"), SNAME("test"), SNAME("method")).is_null());
	}
}
} // namespace GDScriptTests
