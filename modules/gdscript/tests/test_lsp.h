/**************************************************************************/
/*  test_lsp.h                                                            */
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

#ifdef TOOLS_ENABLED

#ifndef GDSCRIPT_NO_LSP

#include "tests/test_macros.h"

#include "../language_server/gdscript_extend_parser.h"
#include "../language_server/gdscript_language_protocol.h"
#include "../language_server/gdscript_workspace.h"
#include "../language_server/godot_lsp.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/doc/editor_help.h"
#include "editor/editor_node.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/regex/regex.h"

#include "thirdparty/doctest/doctest.h"

class TestGDScriptLanguageProtocolInitializer {
public:
	static void setup_client() {
		GDScriptLanguageProtocol *proto = GDScriptLanguageProtocol::get_singleton();
		Ref<GDScriptLanguageProtocol::LSPeer> peer = memnew(GDScriptLanguageProtocol::LSPeer);
		proto->clients.insert(proto->next_client_id, peer);
		proto->latest_client_id = proto->next_client_id;
		proto->next_client_id++;
	}

	static void mark_initialized(GDScriptLanguageProtocol *p_proto) {
		p_proto->_initialized = true;
	}

	static Array take_client_notifications(GDScriptLanguageProtocol *p_proto, const String &p_method) {
		Array notifications;
		if (p_proto == nullptr ||
				p_proto->latest_client_id == LSP_NO_CLIENT ||
				!p_proto->clients.has(p_proto->latest_client_id)) {
			return notifications;
		}

		Ref<GDScriptLanguageProtocol::LSPeer> peer = p_proto->clients.get(p_proto->latest_client_id);
		while (!peer->res_queue.is_empty()) {
			const CharString message_utf8 = peer->res_queue[0];
			peer->res_queue.remove_at(0);

			String message = String::utf8(message_utf8.get_data(), message_utf8.length());
			const int body_start = message.find("\r\n\r\n");
			if (body_start == -1) {
				continue;
			}

			Variant parsed = JSON::parse_string(message.substr(body_start + 4));
			if (parsed.get_type() != Variant::DICTIONARY) {
				continue;
			}

			Dictionary notification = parsed;
			if (String(notification.get("method", "")) == p_method) {
				notifications.push_back(notification);
			}
		}
		return notifications;
	}
};

template <>
struct doctest::StringMaker<LSP::Position> {
	static doctest::String convert(const LSP::Position &p_val) {
		return p_val.to_string().utf8().get_data();
	}
};

template <>
struct doctest::StringMaker<LSP::Range> {
	static doctest::String convert(const LSP::Range &p_val) {
		return p_val.to_string().utf8().get_data();
	}
};

template <>
struct doctest::StringMaker<GodotPosition> {
	static doctest::String convert(const GodotPosition &p_val) {
		return p_val.to_string().utf8().get_data();
	}
};

namespace GDScriptTests {

// LSP GDScript test scripts are located inside project of other GDScript tests:
// Cannot reset `ProjectSettings` (singleton) -> Cannot load another workspace and resources in there.
// -> Reuse GDScript test project. LSP specific scripts are then placed inside `lsp` folder.
//    Access via `res://lsp/my_script.gd`.
const String root = "modules/gdscript/tests/scripts/";

/*
 * After use:
 * * `memdelete` returned `GDScriptLanguageProtocol`.
 * * Call `GDScriptTests::::finish_language`.
 */
GDScriptLanguageProtocol *initialize(const String &p_root) {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(p_root, &err));
	REQUIRE_MESSAGE(err == OK, "Could not open specified root directory");
	String absolute_root = dir->get_current_dir();
	init_language(absolute_root);

	GDScriptLanguageProtocol *proto = memnew(GDScriptLanguageProtocol);
	TestGDScriptLanguageProtocolInitializer::setup_client();

	Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
	workspace->root = absolute_root;
	// On windows: `C:/...` -> `C%3A/...`.
	workspace->root_uri = "file:///" + absolute_root.lstrip("/").replace_first(":", "%3A");

	return proto;
}

LSP::Position pos(const int p_line, const int p_character) {
	LSP::Position p;
	p.line = p_line;
	p.character = p_character;
	return p;
}

LSP::Range range(const LSP::Position p_start, const LSP::Position p_end) {
	LSP::Range r;
	r.start = p_start;
	r.end = p_end;
	return r;
}

LSP::TextDocumentPositionParams pos_in(const LSP::DocumentUri &p_uri, const LSP::Position p_pos) {
	LSP::TextDocumentPositionParams params;
	params.textDocument.uri = p_uri;
	params.position = p_pos;
	return params;
}

Dictionary make_text_document_item(const String &p_uri, const String &p_source) {
	Dictionary text_document;
	text_document["uri"] = p_uri;
	text_document["languageId"] = "gdscript";
	text_document["version"] = 1;
	text_document["text"] = p_source;
	return text_document;
}

Dictionary make_did_open_params(const String &p_uri, const String &p_source) {
	Dictionary params;
	params["textDocument"] = make_text_document_item(p_uri, p_source);
	return params;
}

Dictionary make_text_document_identifier(const String &p_uri) {
	Dictionary text_document;
	text_document["uri"] = p_uri;
	return text_document;
}

Dictionary make_did_change_params(const String &p_uri, const String &p_source) {
	Dictionary change;
	change["text"] = p_source;

	Array changes;
	changes.push_back(change);

	Dictionary params;
	params["textDocument"] = make_text_document_identifier(p_uri);
	params["contentChanges"] = changes;
	return params;
}

Dictionary make_code_action_params(const String &p_uri, const LSP::Range &p_range, const Array &p_only = Array()) {
	Dictionary context;
	context["diagnostics"] = Array();
	if (!p_only.is_empty()) {
		context["only"] = p_only;
	}

	Dictionary params;
	params["textDocument"] = make_text_document_identifier(p_uri);
	params["range"] = p_range.to_json();
	params["context"] = context;
	return params;
}

Dictionary make_rename_params(const String &p_uri, const LSP::Position &p_position, const String &p_new_name) {
	Dictionary params = pos_in(p_uri, p_position).to_json();
	params["newName"] = p_new_name;
	return params;
}

String first_workspace_edit_text(const Dictionary &p_workspace_edit, const String &p_uri) {
	REQUIRE(p_workspace_edit.has("changes"));
	Dictionary changes = p_workspace_edit["changes"];
	REQUIRE(changes.has(p_uri));
	Array edits = changes[p_uri];
	REQUIRE_FALSE(edits.is_empty());
	Dictionary first_edit = edits[0];
	return first_edit["newText"];
}

Array workspace_edits_for_uri(const Dictionary &p_workspace_edit, const String &p_uri) {
	REQUIRE(p_workspace_edit.has("changes"));
	Dictionary changes = p_workspace_edit["changes"];
	REQUIRE(changes.has(p_uri));
	return changes[p_uri];
}

Dictionary first_code_action_with_kind(const Array &p_actions, const String &p_kind) {
	for (int i = 0; i < p_actions.size(); i++) {
		Dictionary action = p_actions[i];
		if (String(action.get("kind", "")) == p_kind) {
			return action;
		}
	}
	return Dictionary();
}

const LSP::DocumentSymbol *test_resolve_symbol_at(const String &p_uri, const LSP::Position p_pos, const String &p_expected_uri, const String &p_expected_name, const LSP::Range &p_expected_range) {
	Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();

	LSP::TextDocumentPositionParams params = pos_in(p_uri, p_pos);
	const LSP::DocumentSymbol *symbol = workspace->resolve_symbol(params);
	CHECK(symbol);

	if (symbol) {
		CHECK_EQ(symbol->uri, p_expected_uri);
		CHECK_EQ(symbol->name, p_expected_name);
		CHECK_EQ(symbol->selectionRange, p_expected_range);
	}

	return symbol;
}

struct InlineTestData {
	LSP::Range range;
	String text;
	String name;
	String ref;

	static bool try_parse(const Vector<String> &p_lines, const int p_line_number, InlineTestData &r_data) {
		String line = p_lines[p_line_number];

		RegEx regex = RegEx("^\\t*#[ |]*(?<range>(?<left><)?\\^+)(\\s+(?<name>(?!->)\\S+))?(\\s+->\\s+(?<ref>\\S+))?");
		Ref<RegExMatch> match = regex.search(line);
		if (match.is_null()) {
			return false;
		}

		// Find first line without leading comment above current line.
		int target_line = p_line_number;
		while (target_line >= 0) {
			String dedented = p_lines[target_line].lstrip("\t");
			if (!dedented.begins_with("#")) {
				break;
			}
			target_line--;
		}
		if (target_line < 0) {
			return false;
		}
		r_data.range.start.line = r_data.range.end.line = target_line;

		String marker = match->get_string("range");
		int i = line.find(marker);
		REQUIRE(i >= 0);
		r_data.range.start.character = i;
		if (!match->get_string("left").is_empty()) {
			// Include `#` (comment char) in range.
			r_data.range.start.character--;
		}
		r_data.range.end.character = i + marker.length();

		String target = p_lines[target_line];
		r_data.text = target.substr(r_data.range.start.character, r_data.range.end.character - r_data.range.start.character);

		r_data.name = match->get_string("name");
		r_data.ref = match->get_string("ref");

		return true;
	}
};

Vector<InlineTestData> read_tests(const String &p_path) {
	Error err;
	String source = FileAccess::get_file_as_string(p_path, &err);
	REQUIRE_MESSAGE(err == OK, vformat("Cannot read '%s'", p_path));

	// Format:
	// ```gdscript
	// var foo = bar + baz
	// #   | |   | |   ^^^ name -> ref
	// #   | |   ^^^ -> ref
	// #   ^^^ name
	//
	// func my_func():
	// #    ^^^^^^^ name
	//     var value = foo + 42
	//     #   ^^^^^ name
	//     print(value)
	//     #     ^^^^^ -> ref
	// ```
	//
	// * `^`: Range marker.
	// * `name`: Unique name. Can contain any characters except whitespace chars.
	// * `ref`: Reference to unique name.
	//
	// Notes:
	// * If range should include first content-char (which is occupied by `#`): use `<` for next marker.
	//   -> Range expands 1 to left (-> includes `#`).
	//   * Note: Means: Range cannot be single char directly marked by `#`, but must be at least two chars (marked with `#<`).
	// * Comment must start at same ident as line its marked (-> because of tab alignment...).
	// * Use spaces to align after `#`! -> for correct alignment
	// * Between `#` and `^` can be spaces or `|` (to better visualize what's marked below).
	PackedStringArray lines = source.split("\n");

	PackedStringArray names;
	Vector<InlineTestData> data;
	for (int i = 0; i < lines.size(); i++) {
		InlineTestData d;
		if (InlineTestData::try_parse(lines, i, d)) {
			if (!d.name.is_empty()) {
				// Safety check: names must be unique.
				if (names.has(d.name)) {
					FAIL(vformat("Duplicated name '%s' in '%s'. Names must be unique!", d.name, p_path));
				}
				names.append(d.name);
			}

			data.append(d);
		}
	}

	return data;
}

void test_resolve_symbol(const String &p_uri, const InlineTestData &p_test_data, const Vector<InlineTestData> &p_all_data) {
	if (p_test_data.ref.is_empty()) {
		return;
	}

	SUBCASE(vformat("Can resolve symbol '%s' at %s to '%s'", p_test_data.text, p_test_data.range.to_string(), p_test_data.ref).utf8().get_data()) {
		const InlineTestData *target = nullptr;
		for (int i = 0; i < p_all_data.size(); i++) {
			if (p_all_data[i].name == p_test_data.ref) {
				target = &p_all_data[i];
				break;
			}
		}
		REQUIRE_MESSAGE(target, vformat("No target for ref '%s'", p_test_data.ref));

		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
		LSP::Position pos = p_test_data.range.start;

		SUBCASE("start of identifier") {
			pos.character = p_test_data.range.start.character;
			test_resolve_symbol_at(p_uri, pos, p_uri, target->text, target->range);
		}

		SUBCASE("inside identifier") {
			pos.character = (p_test_data.range.end.character + p_test_data.range.start.character) / 2;
			test_resolve_symbol_at(p_uri, pos, p_uri, target->text, target->range);
		}

		SUBCASE("end of identifier") {
			pos.character = p_test_data.range.end.character;
			test_resolve_symbol_at(p_uri, pos, p_uri, target->text, target->range);
		}
	}
}

Vector<InlineTestData> filter_ref_towards(const Vector<InlineTestData> &p_data, const String &p_name) {
	Vector<InlineTestData> res;

	for (const InlineTestData &d : p_data) {
		if (d.ref == p_name) {
			res.append(d);
		}
	}

	return res;
}

void test_resolve_symbols(const String &p_uri, const Vector<InlineTestData> &p_test_data, const Vector<InlineTestData> &p_all_data) {
	for (const InlineTestData &d : p_test_data) {
		test_resolve_symbol(p_uri, d, p_all_data);
	}
}

void assert_no_errors_in(const String &p_path) {
	Error err;
	String source = FileAccess::get_file_as_string(p_path, &err);
	REQUIRE_MESSAGE(err == OK, vformat("Cannot read '%s'", p_path));

	GDScriptParser parser;
	err = parser.parse(source, p_path, true);
	REQUIRE_MESSAGE(err == OK, vformat("Errors while parsing '%s'", p_path));

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE_MESSAGE(err == OK, vformat("Errors while analyzing '%s'", p_path));
}

static void restore_lsp_global_script_classes(const Array &p_classes) {
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

struct LSPGlobalScriptClassBackup {
	Array classes;

	LSPGlobalScriptClassBackup() {
		classes = ProjectSettings::get_singleton()->get_global_class_list();
	}

	~LSPGlobalScriptClassBackup() {
		restore_lsp_global_script_classes(classes);
	}
};

void register_lsp_namespace_global_classes() {
	// The global registry is test-scoped; construct LSPGlobalScriptClassBackup before calling this helper.
	ScriptServer::global_classes_clear();
	const StringName language = GDScriptLanguage::get_singleton()->get_name();
	ScriptServer::add_global_class("lsp.characters.LspBaseCharacter", "Node", language, "res://lsp/namespace_lsp_base.gd", false, false);
	ScriptServer::add_global_class("lsp.characters.controllers.LspMyCharacterController", "Node", language, "res://lsp/namespace_lsp_controller.gd", false, false);
	ScriptServer::add_global_class("lsp.characters.LspNamespaceUser", "Node", language, "res://lsp/namespace_lsp_user.gd", false, false);
	ScriptServer::add_global_class("lsp.ambiguous.first.LspAmbiguousClass", "Node", language, "res://lsp/namespace_lsp_ambiguous_first.gd", false, false);
	ScriptServer::add_global_class("lsp.ambiguous.second.LspAmbiguousClass", "Node", language, "res://lsp/namespace_lsp_ambiguous_second.gd", false, false);
	ScriptServer::add_global_class("lsp.ambiguous.LspAmbiguousNamespaceUser", "Node", language, "res://lsp/namespace_lsp_ambiguous_user.gd", false, false);
}

inline LSP::Position lsp_pos(int line, int character) {
	LSP::Position p;
	p.line = line;
	p.character = character;
	return p;
}

void test_position_roundtrip(LSP::Position p_lsp, GodotPosition p_gd, const PackedStringArray &p_lines) {
	GodotPosition actual_gd = GodotPosition::from_lsp(p_lsp, p_lines);
	CHECK_EQ(p_gd, actual_gd);
	LSP::Position actual_lsp = p_gd.to_lsp(p_lines);
	CHECK_EQ(p_lsp, actual_lsp);
}

// Note:
// * Cursor is BETWEEN chars
//	 * `va|r` -> cursor between `a`&`r`
//   * `var`
//        ^
//      -> Character on `r` -> cursor between `a`&`r`s for tests:
// * Line & Char:
//   * LSP: both 0-based
//   * Godot: both 1-based
TEST_SUITE("[Modules][GDScript][LSP][Editor]") {
	TEST_CASE("Can convert positions to and from Godot") {
		String code = R"(extends Node

var member := 42

func f():
		var value := 42
		return value + member)";
		PackedStringArray lines = code.split("\n");

		SUBCASE("line after end") {
			LSP::Position lsp = lsp_pos(7, 0);
			GodotPosition gd(8, 1);
			test_position_roundtrip(lsp, gd, lines);
		}
		SUBCASE("first char in first line") {
			LSP::Position lsp = lsp_pos(0, 0);
			GodotPosition gd(1, 1);
			test_position_roundtrip(lsp, gd, lines);
		}

		SUBCASE("with tabs") {
			// On `v` in `value` in `var value := ...`.
			LSP::Position lsp = lsp_pos(5, 6);
			GodotPosition gd(6, 13);
			test_position_roundtrip(lsp, gd, lines);
		}

		SUBCASE("doesn't fail with column outside of character length") {
			LSP::Position lsp = lsp_pos(2, 100);
			GodotPosition::from_lsp(lsp, lines);

			GodotPosition gd(3, 100);
			gd.to_lsp(lines);
		}

		SUBCASE("doesn't fail with line outside of line length") {
			LSP::Position lsp = lsp_pos(200, 100);
			GodotPosition::from_lsp(lsp, lines);

			GodotPosition gd(300, 100);
			gd.to_lsp(lines);
		}

		SUBCASE("special case: zero column for root class") {
			GodotPosition gd(1, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: zero line and column for root class") {
			GodotPosition gd(0, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: negative line for root class") {
			GodotPosition gd(-1, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: lines.length() + 1 for root class") {
			GodotPosition gd(lines.size() + 1, 0);
			LSP::Position expected = lsp_pos(lines.size(), 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
	}
	TEST_CASE("[workspace][resolve_symbol]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();

		{
			String path = "res://lsp/local_variables.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			SUBCASE("Can get correct ranges for public variables") {
				Vector<InlineTestData> test_data = filter_ref_towards(all_test_data, "member");
				test_resolve_symbols(uri, test_data, all_test_data);
			}
			SUBCASE("Can get correct ranges for local variables") {
				Vector<InlineTestData> test_data = filter_ref_towards(all_test_data, "test");
				test_resolve_symbols(uri, test_data, all_test_data);
			}
			SUBCASE("Can get correct ranges for local parameters") {
				Vector<InlineTestData> test_data = filter_ref_towards(all_test_data, "arg");
				test_resolve_symbols(uri, test_data, all_test_data);
			}
		}

		SUBCASE("Can get correct ranges for indented variables") {
			String path = "res://lsp/indentation.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for scopes") {
			String path = "res://lsp/scopes.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for lambda") {
			String path = "res://lsp/lambdas.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for inner class") {
			String path = "res://lsp/class.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for inner class") {
			String path = "res://lsp/enums.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for shadowing & shadowed variables") {
			String path = "res://lsp/shadowing_initializer.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for properties and getter/setter") {
			String path = "res://lsp/properties.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}
	TEST_CASE("[workspace][document_symbol]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();

		SUBCASE("selectionRange of root class must be inside range") {
			LocalVector<String> paths = {
				"res://lsp/first_line_comment.gd", // Comment on first line
				"res://lsp/first_line_class_name.gd", // class_name (and thus selection range) before extends
			};

			for (const String &path : paths) {
				assert_no_errors_in(path);
				ExtendGDScriptParser *parser = GDScriptLanguageProtocol::get_singleton()->get_parse_result(path);
				REQUIRE(parser);
				LSP::DocumentSymbol cls = parser->get_symbols();

				REQUIRE(((cls.range.start.line == cls.selectionRange.start.line && cls.range.start.character <= cls.selectionRange.start.character) || (cls.range.start.line < cls.selectionRange.start.line)));
				REQUIRE(((cls.range.end.line == cls.selectionRange.end.line && cls.range.end.character >= cls.selectionRange.end.character) || (cls.range.end.line > cls.selectionRange.end.line)));
			}
		}

		SUBCASE("Documentation is correctly set") {
			String path = "res://lsp/doc_comments.gd";
			assert_no_errors_in(path);
			ExtendGDScriptParser *parser = GDScriptLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			LSP::DocumentSymbol cls = parser->get_symbols();
			REQUIRE(cls.documentation.contains("brief"));
			REQUIRE(cls.documentation.contains("description"));
			REQUIRE(cls.documentation.contains("t1"));
			REQUIRE(cls.documentation.contains("t2"));
			REQUIRE(cls.documentation.contains("t3"));
		}

		SUBCASE("Strict type syntax is preserved in symbols and generated API") {
			String path = "res://lsp/strict_type_presentation.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			ExtendGDScriptParser *parser = GDScriptLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			Ref<GDScriptTextDocument> text_document = proto->get_text_document();

			const LSP::DocumentSymbol *maybe_node = parser->get_member_symbol("maybe_node");
			REQUIRE(maybe_node);
			CHECK_EQ(maybe_node->detail, "var maybe_node: Node?");

			const LSP::DocumentSymbol *callback = parser->get_member_symbol("callback");
			REQUIRE(callback);
			CHECK_EQ(callback->detail, "var callback: Callable[[Node?], String]");

			const LSP::DocumentSymbol *payloads = parser->get_member_symbol("payloads");
			REQUIRE(payloads);
			CHECK_EQ(payloads->detail, "var payloads: Dictionary[String, Array[int]]");

			const LSP::DocumentSymbol *event = parser->get_member_symbol("event");
			REQUIRE(event);
			CHECK_EQ(event->detail, "var event: Signal[[String]]");

			const LSP::DocumentSymbol *selected = parser->get_member_symbol("selected");
			REQUIRE(selected);
			CHECK_EQ(selected->detail, "signal selected(node: Node?, callbacks: Array[Callable[[int], void]])");
			REQUIRE(selected->children.size() == 2);
			CHECK_EQ(selected->children[0].detail, "var node: Node?");
			CHECK_EQ(selected->children[1].detail, "var callbacks: Array[Callable[[int], void]]");

			const LSP::DocumentSymbol *describe = parser->get_member_symbol("describe");
			REQUIRE(describe);
			CHECK_EQ(describe->detail, "func describe(handler: Callable[[Node?], String], values: Dictionary[String, Array[int]]) -> Signal[[String]]");

			const LSP::DocumentSymbol *fetch_description = parser->get_member_symbol("fetch_description");
			REQUIRE(fetch_description);
			CHECK_EQ(fetch_description->detail, "async func fetch_description() -> String");

			Variant hover_variant = text_document->hover(pos_in(uri, callback->selectionRange.start).to_json());
			REQUIRE(hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary hover = hover_variant;
			Dictionary hover_contents = hover["contents"];
			CHECK(String(hover_contents["value"]).contains("var callback: Callable[[Node?], String]"));

			const Array &completion_items = parser->get_member_completions();
			Dictionary callback_completion;
			Dictionary event_completion;
			Dictionary describe_completion;
			Dictionary fetch_description_completion;
			for (int i = 0; i < completion_items.size(); i++) {
				Dictionary completion = completion_items[i];
				const String label = completion["label"];
				if (label == "callback") {
					callback_completion = completion;
				} else if (label == "event") {
					event_completion = completion;
				} else if (label == "describe") {
					describe_completion = completion;
				} else if (label == "fetch_description") {
					fetch_description_completion = completion;
				}
			}
			REQUIRE(!callback_completion.is_empty());
			REQUIRE(!event_completion.is_empty());
			REQUIRE(!describe_completion.is_empty());
			REQUIRE(!fetch_description_completion.is_empty());
			Dictionary resolved_callback_completion = text_document->resolve(callback_completion);
			Dictionary resolved_event_completion = text_document->resolve(event_completion);
			Dictionary resolved_describe_completion = text_document->resolve(describe_completion);
			Dictionary resolved_fetch_description_completion = text_document->resolve(fetch_description_completion);
			CHECK_EQ(String(resolved_callback_completion["detail"]), "var callback: Callable[[Node?], String]");
			CHECK_EQ(String(resolved_event_completion["detail"]), "var event: Signal[[String]]");
			CHECK_EQ(String(resolved_describe_completion["detail"]), "func describe(handler: Callable[[Node?], String], values: Dictionary[String, Array[int]]) -> Signal[[String]]");
			CHECK_EQ(String(resolved_fetch_description_completion["detail"]), "async func fetch_description() -> String");

			LSP::SignatureHelp signature_help;
			CHECK_EQ(workspace->resolve_signature(pos_in(uri, pos(13, 19)), signature_help), OK);
			REQUIRE(signature_help.signatures.size() == 1);
			const LSP::SignatureInformation &signature = signature_help.signatures[0];
			CHECK_EQ(signature.label, "func describe(handler: Callable[[Node?], String], values: Dictionary[String, Array[int]]) -> Signal[[String]]");
			REQUIRE(signature.parameters.size() == 2);
			CHECK_EQ(signature.parameters[0].label, "handler: Callable[[Node?], String]");
			CHECK_EQ(signature.parameters[1].label, "values: Dictionary[String, Array[int]]");

			Dictionary api = parser->generate_api();
			Array signals = api["signals"];
			REQUIRE(signals.size() == 1);
			Dictionary signal_api = signals[0];
			CHECK_EQ(String(signal_api["signature"]), "signal selected(node: Node?, callbacks: Array[Callable[[int], void]])");

			Array methods = api["methods"];
			Dictionary method_api;
			Dictionary async_method_api;
			for (int i = 0; i < methods.size(); i++) {
				Dictionary method = methods[i];
				if (String(method["name"]) == "describe") {
					method_api = method;
				} else if (String(method["name"]) == "fetch_description") {
					async_method_api = method;
				}
			}
			REQUIRE(!method_api.is_empty());
			CHECK_EQ(String(method_api["return_type"]), "Signal[[String]]");
			CHECK_EQ(String(method_api["signature"]), "func describe(handler: Callable[[Node?], String], values: Dictionary[String, Array[int]]) -> Signal[[String]]");
			REQUIRE(!async_method_api.is_empty());
			CHECK_EQ(String(async_method_api["return_type"]), "String");
			CHECK_EQ(String(async_method_api["signature"]), "async func fetch_description() -> String");

			Array arguments = method_api["arguments"];
			REQUIRE(arguments.size() == 2);
			Dictionary handler_argument = arguments[0];
			Dictionary values_argument = arguments[1];
			CHECK_EQ(String(handler_argument["type"]), "Callable[[Node?], String]");
			CHECK_EQ(String(values_argument["type"]), "Dictionary[String, Array[int]]");
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[textDocument][definition] resolves GDScript namespaces") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		LSPGlobalScriptClassBackup global_class_backup;
		register_lsp_namespace_global_classes();

		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
		const String user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_user.gd");
		const String base_uri = workspace->get_file_uri("res://lsp/namespace_lsp_base.gd");
		const String controller_uri = workspace->get_file_uri("res://lsp/namespace_lsp_controller.gd");

		assert_no_errors_in("res://lsp/namespace_lsp_user.gd");

		const LSP::Range base_selection = range(pos(1, 11), pos(1, 27));
		test_resolve_symbol_at(user_uri, pos(5, 21), base_uri, "LspBaseCharacter", base_selection);
		test_resolve_symbol_at(user_uri, pos(7, 37), base_uri, "LspBaseCharacter", base_selection);
		test_resolve_symbol_at(user_uri, pos(10, 14), base_uri, "LspBaseCharacter", base_selection);
		test_resolve_symbol_at(user_uri, pos(11, 34), base_uri, "LspBaseCharacter", base_selection);

		test_resolve_symbol_at(user_uri, pos(6, 33), controller_uri, "LspMyCharacterController", range(pos(1, 11), pos(1, 35)));

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[textDocument][definition] leaves ambiguous GDScript namespace imports unresolved") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		LSPGlobalScriptClassBackup global_class_backup;
		register_lsp_namespace_global_classes();

		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
		const String namespace_user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_user.gd");
		const String base_uri = workspace->get_file_uri("res://lsp/namespace_lsp_base.gd");
		const String user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_ambiguous_user.gd");

		assert_no_errors_in("res://lsp/namespace_lsp_user.gd");
		test_resolve_symbol_at(namespace_user_uri, pos(5, 21), base_uri, "LspBaseCharacter", range(pos(1, 11), pos(1, 27)));

		const LSP::DocumentSymbol *symbol = workspace->resolve_symbol(pos_in(user_uri, pos(6, 16)));
		CHECK_FALSE(symbol);

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[textDocument][rename] updates GDScript namespace references") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		LSPGlobalScriptClassBackup global_class_backup;
		register_lsp_namespace_global_classes();

		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
		Ref<GDScriptTextDocument> text_document = proto->get_text_document();
		const String user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_user.gd");
		const String base_uri = workspace->get_file_uri("res://lsp/namespace_lsp_base.gd");
		const String controller_uri = workspace->get_file_uri("res://lsp/namespace_lsp_controller.gd");

		assert_no_errors_in("res://lsp/namespace_lsp_user.gd");

		Dictionary edit = text_document->rename(make_rename_params(base_uri, pos(1, 12), "RenamedBaseCharacter"));

		CHECK_EQ(workspace_edits_for_uri(edit, base_uri).size(), 1);
		CHECK_EQ(workspace_edits_for_uri(edit, user_uri).size(), 4);
		Dictionary changes = edit["changes"];
		CHECK_FALSE(changes.has(controller_uri));

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[textDocument][codeAction] exposes refactors") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
		Ref<GDScriptTextDocument> text_document = proto->get_text_document();

		SUBCASE("server capabilities advertise code actions") {
			TestGDScriptLanguageProtocolInitializer::mark_initialized(proto);

			Dictionary init_params;
			init_params["rootUri"] = workspace->root_uri;
			init_params["rootPath"] = workspace->root;

			Dictionary request;
			request["jsonrpc"] = "2.0";
			request["id"] = 1;
			request["method"] = "initialize";
			request["params"] = init_params;

			Dictionary response = proto->process_action(request);
			Dictionary result = response["result"];
			Dictionary capabilities = result["capabilities"];
			REQUIRE(capabilities["codeActionProvider"].get_type() == Variant::DICTIONARY);

			Dictionary code_action_provider = capabilities["codeActionProvider"];
			CHECK(bool(code_action_provider["resolveProvider"]));
			Array kinds = code_action_provider["codeActionKinds"];
			CHECK(kinds.has("refactor.extract"));
			CHECK(kinds.has("refactor.rewrite"));
			CHECK(kinds.has("refactor.inline"));
			CHECK_FALSE(kinds.has("refactor.rename"));
		}

		SUBCASE("returns Add Type Annotation and resolves its workspace edit") {
			const String source = "var score = 1\n";
			const String uri = workspace->get_file_uri("res://lsp/code_action_type_annotation.gd");
			text_document->didOpen(make_did_open_params(uri, source));

			Array only;
			only.push_back("refactor");
			Array actions = text_document->codeAction(make_code_action_params(uri, range(pos(0, 1), pos(0, 1)), only));
			Dictionary action = first_code_action_with_kind(actions, "refactor.rewrite");
			REQUIRE_FALSE(action.is_empty());
			CHECK_EQ(String(action["title"]), "Add Type Annotation");
			CHECK_FALSE(action.has("edit"));

			Dictionary data = action["data"];
			data["clientPayload"] = "keep-me";
			action["data"] = data;

			Dictionary resolved = text_document->resolveCodeAction(action);
			REQUIRE(resolved.has("edit"));
			Dictionary edit = resolved["edit"];
			CHECK_EQ(first_workspace_edit_text(edit, uri), ": int = ");

			Dictionary resolved_data = resolved["data"];
			CHECK_EQ(String(resolved_data["clientPayload"]), "keep-me");
		}

		SUBCASE("notifies when a stale code action cannot resolve") {
			const String uri = workspace->get_file_uri("res://lsp/code_action_stale.gd");
			text_document->didOpen(make_did_open_params(uri, "var score = 1\n"));

			Array actions = text_document->codeAction(make_code_action_params(uri, range(pos(0, 1), pos(0, 1))));
			Dictionary action = first_code_action_with_kind(actions, "refactor.rewrite");
			REQUIRE_FALSE(action.is_empty());

			text_document->didChange(make_did_change_params(uri, "var score: int = 1\n"));
			Dictionary resolved = text_document->resolveCodeAction(action);
			CHECK_FALSE(resolved.has("edit"));

			Array notifications = TestGDScriptLanguageProtocolInitializer::take_client_notifications(
					proto, "window/showMessage");
			CHECK_FALSE(notifications.is_empty());
			if (!notifications.is_empty()) {
				Dictionary notification = notifications[0];
				Dictionary params = notification["params"];
				CHECK_EQ(int(params["type"]), LSP::MessageType::Error);
				CHECK(String(params["message"]).contains("Cannot resolve code action"));
			}
		}

		SUBCASE("does not list Rename as a code action") {
			const String uri = workspace->get_file_uri("res://refactor/rename_local.gd");

			Array actions = text_document->codeAction(make_code_action_params(uri, range(pos(3, 5), pos(3, 5))));
			Dictionary action = first_code_action_with_kind(actions, "refactor.rename");
			CHECK(action.is_empty());
		}

		SUBCASE("filters code actions by requested kind") {
			const String source = "var score = 1\n";
			const String uri = workspace->get_file_uri("res://lsp/code_action_filter.gd");
			text_document->didOpen(make_did_open_params(uri, source));

			Array rewrite_only;
			rewrite_only.push_back("refactor.rewrite");
			Dictionary rewrite_params = make_code_action_params(uri, range(pos(0, 1), pos(0, 1)), rewrite_only);
			Array rewrite_actions = text_document->codeAction(rewrite_params);
			CHECK_FALSE(first_code_action_with_kind(rewrite_actions, "refactor.rewrite").is_empty());

			Array extract_only;
			extract_only.push_back("refactor.extract");
			Dictionary extract_params = make_code_action_params(uri, range(pos(0, 1), pos(0, 1)), extract_only);
			Array extract_actions = text_document->codeAction(extract_params);
			CHECK(first_code_action_with_kind(extract_actions, "refactor.rewrite").is_empty());
		}

		SUBCASE("textDocument rename returns grouped cross-file edits") {
			GDScriptTests::assert_no_errors_in("res://refactor/rename_cross_file_user.gd");

			const String target_uri = workspace->get_file_uri("res://refactor/rename_cross_file_target.gd");
			const String user_uri = workspace->get_file_uri("res://refactor/rename_cross_file_user.gd");
			Dictionary edit = text_document->rename(make_rename_params(target_uri, pos(2, 5), "renamed_count"));

			CHECK_EQ(workspace_edits_for_uri(edit, target_uri).size(), 3);
			CHECK_EQ(workspace_edits_for_uri(edit, user_uri).size(), 2);
		}

		SUBCASE("textDocument rename returns exported cross-file script edits") {
			GDScriptTests::assert_no_errors_in("res://refactor/rename_cross_file_exported_user.gd");

			const String target_uri = workspace->get_file_uri("res://refactor/rename_cross_file_exported_target.gd");
			const String user_uri = workspace->get_file_uri("res://refactor/rename_cross_file_exported_user.gd");
			const String scene_uri = workspace->get_file_uri("res://refactor/rename_cross_file_exported_scene.tscn");
			Dictionary rename_params = make_rename_params(target_uri, pos(2, 13), "renamed_exported_count");
			Dictionary edit = text_document->rename(rename_params);

			CHECK_EQ(workspace_edits_for_uri(edit, target_uri).size(), 3);
			CHECK_EQ(workspace_edits_for_uri(edit, user_uri).size(), 2);
			Dictionary changes = edit["changes"];
			CHECK_FALSE(changes.has(scene_uri));

			Array notifications = TestGDScriptLanguageProtocolInitializer::take_client_notifications(
					proto, "window/showMessage");
			CHECK_FALSE(notifications.is_empty());
			if (!notifications.is_empty()) {
				Dictionary notification = notifications[0];
				Dictionary params = notification["params"];
				CHECK_EQ(int(params["type"]), LSP::MessageType::Warning);
				CHECK(String(params["message"]).to_lower().contains("exported"));
			}
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("BBCode to markdown conversion") {
		// This tests the conversion from BBCode docstrings to the markdown markup sent to
		// the LSP client on documentation requests

		// Basic formatting
		CHECK_EQ(LSP::marked_documentation("[b]bold[/b]"), "**bold**");
		CHECK_EQ(LSP::marked_documentation("[i]italic[/i]"), "*italic*");
		CHECK_EQ(LSP::marked_documentation("[u]underline[/u]"), "__underline__");
		CHECK_EQ(LSP::marked_documentation("[s]strikethrough[/s]"), "~~strikethrough~~");
		CHECK_EQ(LSP::marked_documentation("[code]code[/code]"), "`code`");
		CHECK_EQ(LSP::marked_documentation("[kbd]Ctrl + S[/kbd]"), "`Ctrl + S`");

		// Line breaks. We insert paragraphs for [br] because the BBCode to
		// markdown conversion function simply makes the conversion line-wise and
		// we don't distinguish markdown inline elements and blocks.
		CHECK_EQ(LSP::marked_documentation("Line1[br]Line2"), "Line1\n\nLine2");

		// These tags (center, color, font) aren't supported in markdown and should be stripped.
		CHECK_EQ(LSP::marked_documentation("[center]Centered text[/center]"), "Centered text");
		CHECK_EQ(LSP::marked_documentation("[color=red]red text[/color]"), "red text");
		CHECK_EQ(LSP::marked_documentation("[font=Arial]Arial text[/font]"), "Arial text");

		// The following tests are for all the link patterns specific to Godot's built-in docs that we render as inline code.
		CHECK_EQ(LSP::marked_documentation("Class link: [Node2D], [Sprite2D]"), "Class link: `Node2D`, `Sprite2D`");
		CHECK_EQ(LSP::marked_documentation("Single class [RigidBody2D]"), "Single class `RigidBody2D`");
		CHECK_EQ(LSP::marked_documentation("[method Node2D.set_position]"), "`Node2D.set_position`");
		CHECK_EQ(LSP::marked_documentation("[member Node2D.position]"), "`Node2D.position`");
		CHECK_EQ(LSP::marked_documentation("[signal Node.ready]"), "`Node.ready`");
		CHECK_EQ(LSP::marked_documentation("[constant Color.RED]"), "`Color.RED`");
		CHECK_EQ(LSP::marked_documentation("[enum Node.ProcessMode]"), "`Node.ProcessMode`");
		CHECK_EQ(LSP::marked_documentation("[annotation @GDScript.@export]"), "`@GDScript.@export`");
		CHECK_EQ(LSP::marked_documentation("[constructor Vector2.Vector2]"), "`Vector2.Vector2`");
		CHECK_EQ(LSP::marked_documentation("[operator Vector2.operator +]"), "`Vector2.operator +`");
		CHECK_EQ(LSP::marked_documentation("[theme_item Button.font]"), "`Button.font`");
		CHECK_EQ(LSP::marked_documentation("[param delta]"), "`delta`");

		// Markdown links
		CHECK_EQ(LSP::marked_documentation("[url=https://godotengine.org]link to Godot Engine[/url]"),
				"[link to Godot Engine](https://godotengine.org)");
		CHECK_EQ(LSP::marked_documentation("[url]https://godotengine.org/[/url]"),
				"[https://godotengine.org/](https://godotengine.org/)");

		// Code listings
		CHECK_EQ(LSP::marked_documentation("[codeblock]\nfunc test():\n    print(\"Hello, Godot!\")\n[/codeblock]"),
				"```gdscript\nfunc test():\n    print(\"Hello, Godot!\")\n```");
		CHECK_EQ(LSP::marked_documentation("[codeblock lang=csharp]\npublic void Test()\n{\n    GD.Print(\"Hello, Godot!\");\n}\n[/codeblock]"),
				"```csharp\npublic void Test()\n{\n    GD.Print(\"Hello, Godot!\");\n}\n```");
		// Code listings with multiple languages (the codeblocks tag is used in the built-in reference)
		// When [codeblocks] is used, we only convert the [gdscript] tag to a code block like the built-in editor.
		// NOTE: There is always a GDScript code listing in the built-in class reference.
		CHECK_EQ(LSP::marked_documentation("[codeblocks]\n[gdscript]\nprint(hash(\"a\")) # Prints 177670\n[/gdscript]\n[csharp]\nGD.Print(GD.Hash(\"a\")); // Prints 177670\n[/csharp]\n[/codeblocks]"),
				"```gdscript\nprint(hash(\"a\")) # Prints 177670\n```\n");

		// lb and rb are used to insert literal square brackets in markdown.
		CHECK_EQ(LSP::marked_documentation("[lb]literal brackets[rb]"), "\\[literal brackets\\]");
		CHECK_EQ(LSP::marked_documentation("[lb]literal[rb] with [ClassName]"), "\\[literal\\] with `ClassName`");

		// We have to be careful that different patterns don't conflict with each
		// other, especially with urls that use brackets in markdown.
		CHECK_EQ(LSP::marked_documentation("Class [Sprite2D] with [url=https://godotengine.org]link[/url]"),
				"Class `Sprite2D` with [link](https://godotengine.org)");
	}
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
