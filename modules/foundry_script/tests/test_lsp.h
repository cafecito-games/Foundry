/**************************************************************************/
/*  test_lsp.h                                                            */
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

#ifdef TOOLS_ENABLED

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "tests/test_macros.h"

#include "../language_server/foundry_lsp.h"
#include "../language_server/fs_extend_parser.h"
#include "../language_server/fs_language_protocol.h"
#include "../language_server/fs_semantic_tokens.h"
#include "../language_server/fs_workspace.h"

#include "fs_temporary_project_tree.h"

#include "core/config/project_build_pipeline_status.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/doc/editor_help.h"
#include "editor/editor_node.h"

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_format.h"
#include "modules/regex/regex.h"

#include "thirdparty/doctest/doctest.h"

class TestFSLanguageProtocolInitializer {
public:
	static void setup_client() {
		FSLanguageProtocol *proto = FSLanguageProtocol::get_singleton();
		Ref<FSLanguageProtocol::LSPeer> peer = memnew(FSLanguageProtocol::LSPeer);
		proto->clients.insert(proto->next_client_id, peer);
		proto->latest_client_id = proto->next_client_id;
		proto->next_client_id++;
	}

	static void mark_initialized(FSLanguageProtocol *p_proto) {
		p_proto->_initialized = true;
	}

	static Array take_client_notifications(FSLanguageProtocol *p_proto, const String &p_method) {
		Array notifications;
		if (p_proto == nullptr ||
				p_proto->latest_client_id == LSP_NO_CLIENT ||
				!p_proto->clients.has(p_proto->latest_client_id)) {
			return notifications;
		}

		Ref<FSLanguageProtocol::LSPeer> peer = p_proto->clients.get(p_proto->latest_client_id);
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
struct doctest::StringMaker<FoundryPosition> {
	static doctest::String convert(const FoundryPosition &p_val) {
		return p_val.to_string().utf8().get_data();
	}
};

namespace FSTests {

// LSP FoundryScript test scripts are located inside project of other FoundryScript tests:
// Cannot reset `ProjectSettings` (singleton) -> Cannot load another workspace and resources in there.
// -> Reuse FoundryScript test project. LSP specific scripts are then placed inside `lsp` folder.
//    Access via `res://lsp/my_script.fs`.
const char *LSP_FIXTURE_PROJECT_ROOT = "modules/foundry_script/tests/scripts";

String get_test_project_root() {
	return TemporaryProjectTree::stage_project_copy(LSP_FIXTURE_PROJECT_ROOT, "foundry_script_lsp_project");
}

struct TestProjectRoot {
	operator String() const {
		return get_test_project_root();
	}
};

const TestProjectRoot root;

struct ScopedLSPTempFile {
	String path;
	String absolute_path;
	bool existed = false;
	String previous_source;

	static String resolve_path(const String &p_path) {
		if (p_path.begins_with("res://")) {
			const String relative_path = p_path.substr(String("res://").length());
			if (FSLanguageProtocol::get_singleton() != nullptr) {
				Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
				if (workspace.is_valid() && !workspace->root.is_empty()) {
					return workspace->root.path_join(relative_path);
				}
			}
			const String resource_path = ProjectSettings::get_singleton()->get_resource_path();
			if (!resource_path.is_empty()) {
				return resource_path.path_join(relative_path);
			}
		}
		return ProjectSettings::get_singleton()->globalize_path(p_path);
	}

	ScopedLSPTempFile(const String &p_path, const String &p_source) {
		path = p_path;
		absolute_path = resolve_path(path);
		const Error mkdir_err = DirAccess::make_dir_recursive_absolute(absolute_path.get_base_dir());
		REQUIRE_EQ(mkdir_err, OK);
		if (FileAccess::exists(absolute_path)) {
			existed = true;
			Ref<FileAccess> existing = FileAccess::open(absolute_path, FileAccess::READ);
			REQUIRE_MESSAGE(existing.is_valid(), vformat("Cannot read existing '%s'", path));
			previous_source = existing->get_as_utf8_string();
		}
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", path));
		file->store_string(p_source);
	}

	~ScopedLSPTempFile() {
		if (existed) {
			Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
			ERR_FAIL_COND(file.is_null());
			file->store_string(previous_source);
		} else {
			DirAccess::remove_absolute(absolute_path);
		}
	}

	static void remove_recursive(const String &p_absolute_path) {
		Ref<DirAccess> dir = DirAccess::open(p_absolute_path);
		if (dir.is_null()) {
			return;
		}
		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_absolute_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(child)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_absolute_path);
	}
};

ProjectBuildPipelineStatusSnapshot make_blocked_pre_compile_snapshot(const String &p_message) {
	ProjectBuildPipelineStatusSnapshot snapshot;
	snapshot.state = ProjectBuildPipelineStatus::STATE_BLOCKED;
	snapshot.blocks_downstream_indexing = true;

	ProjectBuildPipelineDiagnostic diagnostic;
	diagnostic.kind = ProjectBuildPipelineDiagnostic::KIND_DIRTY;
	diagnostic.task_name = "generate";
	diagnostic.provider_id = "command";
	diagnostic.file = "res://project.foundry";
	diagnostic.message = p_message;
	diagnostic.dirty_reason = ProjectBuildState::DIRTY_PREVIOUS_FAILURE;
	snapshot.diagnostics.push_back(diagnostic);

	return snapshot;
}

ProjectBuildPipelineStatusSnapshot make_clean_pre_compile_snapshot() {
	ProjectBuildPipelineStatusSnapshot snapshot;
	snapshot.state = ProjectBuildPipelineStatus::STATE_CLEAN;
	snapshot.blocks_downstream_indexing = false;
	return snapshot;
}

/*
 * After use:
 * * `memdelete` returned `FSLanguageProtocol`.
 * * Call `FSTests::::finish_language`.
 */
FSLanguageProtocol *initialize(const String &p_root) {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(p_root, &err));
	REQUIRE_MESSAGE(err == OK, "Could not open specified root directory");
	// doctest's REQUIRE does not unwind the stack in this engine's `-fno-exceptions`
	// builds; a failed REQUIRE above still falls through to this line, so guard the
	// dereference explicitly instead of crashing the whole test process on a null `dir`.
	if (dir.is_null()) {
		return nullptr;
	}
	String absolute_root = dir->get_current_dir();
	init_language(absolute_root);

	FSLanguageProtocol *proto = memnew(FSLanguageProtocol);
	TestFSLanguageProtocolInitializer::setup_client();

	Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
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
	text_document["languageId"] = "foundry_script";
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

Dictionary make_formatting_params(const String &p_uri) {
	Dictionary options;
	options["tabSize"] = 4;
	options["insertSpaces"] = false;

	Dictionary params;
	params["textDocument"] = make_text_document_identifier(p_uri);
	params["options"] = options;
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

bool text_edits_include(const Array &p_edits, int p_line, int p_start_character, int p_end_character, const String &p_new_text) {
	for (int i = 0; i < p_edits.size(); i++) {
		Dictionary edit = p_edits[i];
		if (String(edit["newText"]) != p_new_text) {
			continue;
		}
		Dictionary range = edit["range"];
		Dictionary start = range["start"];
		Dictionary end = range["end"];
		if (int(start["line"]) == p_line &&
				int(start["character"]) == p_start_character &&
				int(end["line"]) == p_line &&
				int(end["character"]) == p_end_character) {
			return true;
		}
	}
	return false;
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
	Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();

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
	// ```foundry_script
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

		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
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

	FSParser parser;
	err = parser.parse(source, p_path, true);
	REQUIRE_MESSAGE(err == OK, vformat("Errors while parsing '%s'", p_path));

	FSAnalyzer analyzer(&parser);
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
		const bool is_trait = c.has("is_trait") && c["is_trait"];
		const bool is_enum = c.has("is_enum") && c["is_enum"];
		ScriptServer::add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"], is_trait, is_enum);
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
	const StringName language = FSLanguage::get_singleton()->get_name();
	ScriptServer::add_global_class("lsp.characters.LspBaseCharacter", "Node", language, "res://lsp/namespace_lsp_base.fs", false, false, false);
	ScriptServer::add_global_class("lsp.characters.controllers.LspMyCharacterController", "Node", language, "res://lsp/namespace_lsp_controller.fs", false, false, false);
	ScriptServer::add_global_class("lsp.characters.LspNamespaceUser", "Node", language, "res://lsp/namespace_lsp_user.fs", false, false, false);
	ScriptServer::add_global_class("lsp.ambiguous.first.LspAmbiguousClass", "Node", language, "res://lsp/namespace_lsp_ambiguous_first.fs", false, false, false);
	ScriptServer::add_global_class("lsp.ambiguous.second.LspAmbiguousClass", "Node", language, "res://lsp/namespace_lsp_ambiguous_second.fs", false, false, false);
	ScriptServer::add_global_class("lsp.ambiguous.LspAmbiguousNamespaceUser", "Node", language, "res://lsp/namespace_lsp_ambiguous_user.fs", false, false, false);
}

inline LSP::Position lsp_pos(int line, int character) {
	LSP::Position p;
	p.line = line;
	p.character = character;
	return p;
}

void test_position_roundtrip(LSP::Position p_lsp, FoundryPosition p_gd, const PackedStringArray &p_lines) {
	FoundryPosition actual_gd = FoundryPosition::from_lsp(p_lsp, p_lines);
	CHECK_EQ(p_gd, actual_gd);
	LSP::Position actual_lsp = p_gd.to_lsp(p_lines);
	CHECK_EQ(p_lsp, actual_lsp);
}

struct ScopedEnvironmentVariable {
	String name;
	bool had_previous = false;
	String previous;

	ScopedEnvironmentVariable(const String &p_name, const String &p_value) {
		name = p_name;
		had_previous = OS::get_singleton()->has_environment(name);
		if (had_previous) {
			previous = OS::get_singleton()->get_environment(name);
		}
		OS::get_singleton()->set_environment(name, p_value);
	}

	~ScopedEnvironmentVariable() {
		if (had_previous) {
			OS::get_singleton()->set_environment(name, previous);
		} else {
			OS::get_singleton()->unset_environment(name);
		}
	}
};

String lsp_scratch_contract_root() {
	return OS::get_singleton()->get_temp_path().path_join("foundry_lsp_scratch_contract");
}

String lsp_fixture_root_absolute() {
	Error err = OK;
	Ref<DirAccess> dir = DirAccess::open("modules/foundry_script/tests/scripts", &err);
	REQUIRE_MESSAGE(err == OK, "Could not open Foundry Script fixture root.");
	return dir->get_current_dir().simplify_path();
}

TEST_CASE("[Modules][FoundryScript][LSP scratch] test project root is staged under scratch") {
	const String scratch_root = lsp_scratch_contract_root();
	ScopedEnvironmentVariable scratch_env("FOUNDRY_TEST_SCRATCH", scratch_root);

	const String test_root = String(root).simplify_path();
	CHECK(test_root.begins_with(scratch_root.path_join("")));
	CHECK_NE(test_root, lsp_fixture_root_absolute());
	CHECK(FileAccess::exists(test_root.path_join("project.foundry")));
}

TEST_CASE("[Modules][FoundryScript][LSP scratch] temp files resolve inside staged project") {
	const String scratch_root = lsp_scratch_contract_root();
	ScopedEnvironmentVariable scratch_env("FOUNDRY_TEST_SCRATCH", scratch_root);
	FSLanguageProtocol *proto = initialize(root);

	const String temp_path = "res://lsp/scratch_probe_generated.txt";
	const String fixture_path = lsp_fixture_root_absolute().path_join("lsp/scratch_probe_generated.txt");
	DirAccess::remove_absolute(fixture_path);

	{
		ScopedLSPTempFile temp(temp_path, "probe\n");
		const String absolute_path = ScopedLSPTempFile::resolve_path(temp_path).simplify_path();
		CHECK(absolute_path.begins_with(scratch_root.path_join("")));
		CHECK_FALSE(FileAccess::exists(fixture_path));
	}

	memdelete(proto);
	finish_language();
}

TEST_CASE("[Modules][FoundryScript][LSP scratch] temp file cleanup uses original resolved path after language teardown") {
	const String scratch_root = lsp_scratch_contract_root();
	ScopedEnvironmentVariable scratch_env("FOUNDRY_TEST_SCRATCH", scratch_root);
	const String test_root = String(root);
	const String repo_project_path = "project.foundry";
	DirAccess::remove_absolute(repo_project_path);

	FSLanguageProtocol *proto = initialize(root);
	{
		ScopedLSPTempFile project_config("res://project.foundry", "[application]\nconfig/name=\"Scratch\"\n");
		memdelete(proto);
		finish_language();
	}

	CHECK_FALSE(FileAccess::exists(repo_project_path));
	CHECK(FileAccess::get_file_as_string(test_root.path_join("project.foundry")).contains("GDScript Integration Test Suite"));
	DirAccess::remove_absolute(repo_project_path);
}

// Note:
// * Cursor is BETWEEN chars
//	 * `va|r` -> cursor between `a`&`r`
//   * `var`
//        ^
//      -> Character on `r` -> cursor between `a`&`r`s for tests:
// * Line & Char:
//   * LSP: both 0-based
//   * Foundry: both 1-based
TEST_SUITE("[Modules][FoundryScript][LSP][Editor]") {
	TEST_CASE("Can convert positions to and from Foundry") {
		String code = R"(extends Node

var member := 42

func f():
		var value := 42
		return value + member)";
		PackedStringArray lines = code.split("\n");

		SUBCASE("line after end") {
			LSP::Position lsp = lsp_pos(7, 0);
			FoundryPosition gd(8, 1);
			test_position_roundtrip(lsp, gd, lines);
		}
		SUBCASE("first char in first line") {
			LSP::Position lsp = lsp_pos(0, 0);
			FoundryPosition gd(1, 1);
			test_position_roundtrip(lsp, gd, lines);
		}

		SUBCASE("with tabs") {
			// On `v` in `value` in `var value := ...`.
			LSP::Position lsp = lsp_pos(5, 6);
			FoundryPosition gd(6, 13);
			test_position_roundtrip(lsp, gd, lines);
		}

		SUBCASE("doesn't fail with column outside of character length") {
			LSP::Position lsp = lsp_pos(2, 100);
			FoundryPosition::from_lsp(lsp, lines);

			FoundryPosition gd(3, 100);
			gd.to_lsp(lines);
		}

		SUBCASE("doesn't fail with line outside of line length") {
			LSP::Position lsp = lsp_pos(200, 100);
			FoundryPosition::from_lsp(lsp, lines);

			FoundryPosition gd(300, 100);
			gd.to_lsp(lines);
		}

		SUBCASE("special case: zero column for root class") {
			FoundryPosition gd(1, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: zero line and column for root class") {
			FoundryPosition gd(0, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: negative line for root class") {
			FoundryPosition gd(-1, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: lines.length() + 1 for root class") {
			FoundryPosition gd(lines.size() + 1, 0);
			LSP::Position expected = lsp_pos(lines.size(), 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
	}
	TEST_CASE("[workspace][resolve_symbol]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();

		{
			String path = "res://lsp/local_variables.fs";
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
			String path = "res://lsp/indentation.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for scopes") {
			String path = "res://lsp/scopes.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for lambda") {
			String path = "res://lsp/lambdas.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for inner class") {
			String path = "res://lsp/class.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for inner class") {
			String path = "res://lsp/enums.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for shadowing & shadowed variables") {
			String path = "res://lsp/shadowing_initializer.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for properties and getter/setter") {
			String path = "res://lsp/properties.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		memdelete(proto);
		memdelete(efs);
	}
	TEST_CASE("[workspace][document_symbol]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();

		SUBCASE("selectionRange of root class must be inside range") {
			LocalVector<String> paths = {
				"res://lsp/first_line_comment.fs", // Comment on first line
				"res://lsp/first_line_class_name.fs", // class_name (and thus selection range) before extends
			};

			for (const String &path : paths) {
				assert_no_errors_in(path);
				ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
				REQUIRE(parser);
				LSP::DocumentSymbol cls = parser->get_symbols();

				REQUIRE(((cls.range.start.line == cls.selectionRange.start.line && cls.range.start.character <= cls.selectionRange.start.character) || (cls.range.start.line < cls.selectionRange.start.line)));
				REQUIRE(((cls.range.end.line == cls.selectionRange.end.line && cls.range.end.character >= cls.selectionRange.end.character) || (cls.range.end.line > cls.selectionRange.end.line)));
			}
		}

		SUBCASE("Traits are reported with the trait symbol kind and detail") {
			String path = "res://lsp/traits.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);

			const LSP::DocumentSymbol *drawable = parser->get_member_symbol("Drawable");
			REQUIRE(drawable);
			CHECK_EQ(drawable->kind, LSP::SymbolKind::Interface);
			CHECK_EQ(drawable->detail, "trait Drawable");

			const LSP::DocumentSymbol *sprite = parser->get_member_symbol("Sprite");
			REQUIRE(sprite);
			CHECK_EQ(sprite->kind, LSP::SymbolKind::Class);
			CHECK_EQ(sprite->detail, "class Sprite");
		}

		SUBCASE("A global trait_name file is reported as a trait") {
			String path = "res://lsp/global_trait.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			LSP::DocumentSymbol cls = parser->get_symbols();
			CHECK_EQ(cls.kind, LSP::SymbolKind::Interface);
			CHECK_EQ(cls.detail, "trait LspGlobalTrait");
		}

		SUBCASE("Tuple declarations are reported with fields") {
			String path = "res://lsp/tuples.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);

			const LSP::DocumentSymbol *position = parser->get_member_symbol("PlayerWorldPosition");
			REQUIRE(position);
			CHECK_EQ(position->kind, LSP::SymbolKind::Struct);
			CHECK_EQ(position->detail, "tuple PlayerWorldPosition");
			CHECK(position->documentation.contains("Player position in the world."));
			REQUIRE(position->children.size() == 2);
			CHECK_EQ(position->children[0].name, "vec");
			CHECK_EQ(position->children[0].kind, LSP::SymbolKind::Field);
			CHECK_EQ(position->children[1].name, "zone");
			CHECK_EQ(position->children[1].kind, LSP::SymbolKind::Field);

			// A positional field has no name, so it contributes no child symbol.
			const LSP::DocumentSymbol *pair = parser->get_member_symbol("Pair");
			REQUIRE(pair);
			CHECK_EQ(pair->kind, LSP::SymbolKind::Struct);
			CHECK(pair->children.is_empty());
		}

		SUBCASE("A global tuple_name file is reported as a tuple with fields") {
			String path = "res://lsp/global_tuple.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			LSP::DocumentSymbol cls = parser->get_symbols();
			CHECK_EQ(cls.kind, LSP::SymbolKind::Struct);
			CHECK_EQ(cls.detail, "tuple LspGlobalTuple");
			REQUIRE(cls.children.size() == 2);
			CHECK_EQ(cls.children[0].name, "x");
			CHECK_EQ(cls.children[0].kind, LSP::SymbolKind::Field);
			CHECK_EQ(cls.children[1].name, "y");
			CHECK_EQ(cls.children[1].kind, LSP::SymbolKind::Field);
		}

		SUBCASE("Tagged-union cases are reported with their payload signatures") {
			String path = "res://lsp/tagged_unions.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);

			const LSP::DocumentSymbol *message = parser->get_member_symbol("Message");
			REQUIRE(message);
			CHECK_EQ(message->kind, LSP::SymbolKind::Enum);
			CHECK(message->documentation.contains("A message the actor can receive."));
			CHECK_EQ(message->detail, "enum Message:\n\tQuit\n\tMove(x: int, y: int)\n\tWrite(text: String)");
			REQUIRE(message->children.size() == 3);

			// A tag is ordinal by declaration order, so it is not spelled as a value; a payload
			// case is described by the payload it declares.
			CHECK_EQ(message->children[0].name, "Quit");
			CHECK_EQ(message->children[0].kind, LSP::SymbolKind::EnumMember);
			CHECK_EQ(message->children[0].detail, "Quit");
			CHECK(message->children[0].documentation.contains("Stop processing."));
			CHECK_EQ(message->children[1].name, "Move");
			CHECK_EQ(message->children[1].kind, LSP::SymbolKind::EnumMember);
			CHECK_EQ(message->children[1].detail, "Move(x: int, y: int)");
			CHECK(message->children[1].documentation.contains("Move by a delta."));
			CHECK_EQ(message->children[2].name, "Write");
			CHECK_EQ(message->children[2].detail, "Write(text: String)");
		}

		SUBCASE("A global enum_name file is reported as an enum with members") {
			LSPGlobalScriptClassBackup global_class_backup;
			ScriptServer::global_classes_clear();

			String path = "res://lsp/global_enum.fs";
			String user_path = "res://lsp/global_enum_user.fs";
			String uri = workspace->get_file_uri(path);
			String user_uri = workspace->get_file_uri(user_path);
			StringName language = FSLanguage::get_singleton()->get_name();
			ScriptServer::add_global_class("LspGlobalEnum", String(), language, path, false, false, false, true);
			assert_no_errors_in(path);
			assert_no_errors_in(user_path);

			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			LSP::DocumentSymbol cls = parser->get_symbols();
			CHECK_EQ(cls.name, "LspGlobalEnum");
			CHECK_EQ(cls.kind, LSP::SymbolKind::Enum);
			CHECK_EQ(cls.detail, "enum LspGlobalEnum");
			CHECK(cls.documentation.contains("Global enum documentation."));
			CHECK_EQ(cls.children.size(), 6);
			if (cls.children.size() == 6) {
				CHECK_EQ(cls.children[0].name, "ALPHA");
				CHECK_EQ(cls.children[0].kind, LSP::SymbolKind::EnumMember);
				CHECK_EQ(cls.children[1].name, "BETA");
				CHECK_EQ(cls.children[1].kind, LSP::SymbolKind::EnumMember);
				CHECK_EQ(cls.children[2].name, "GAMMA");
				CHECK_EQ(cls.children[2].kind, LSP::SymbolKind::EnumMember);
				CHECK_EQ(cls.children[3].name, "label");
				CHECK_EQ(cls.children[3].kind, LSP::SymbolKind::Method);
				CHECK_EQ(cls.children[3].detail, R"(func label(prefix: String = "") -> String)");
				CHECK(cls.children[3].documentation.contains("Formats a global enum value."));
				CHECK_EQ(cls.children[4].name, "parse");
				CHECK_EQ(cls.children[4].kind, LSP::SymbolKind::Function);
				CHECK_EQ(cls.children[4].detail, "static func parse(text: String) -> LspGlobalEnum");
				CHECK_EQ(cls.children[5].name, "load");
				CHECK_EQ(cls.children[5].kind, LSP::SymbolKind::Function);
				CHECK_EQ(cls.children[5].detail, "static async func load(text: String) -> String");
			}

			Ref<FSTextDocument> text_document = proto->get_text_document();
			Dictionary document_symbol_params;
			document_symbol_params["textDocument"] = make_text_document_identifier(uri);
			Array document_symbols = text_document->documentSymbol(document_symbol_params);
			REQUIRE(document_symbols.size() == 1);
			Dictionary root_symbol = document_symbols[0];
			CHECK_EQ(String(root_symbol["name"]), "LspGlobalEnum");
			CHECK_EQ(int(root_symbol["kind"]), LSP::SymbolKind::Enum);
			CHECK_EQ(String(root_symbol["detail"]), "enum LspGlobalEnum");
			CHECK(root_symbol.has("children"));
			Array child_symbols;
			if (root_symbol.has("children")) {
				child_symbols = root_symbol["children"];
			}
			CHECK_EQ(child_symbols.size(), 6);
			if (child_symbols.size() == 6) {
				Dictionary alpha_symbol = child_symbols[0];
				Dictionary beta_symbol = child_symbols[1];
				Dictionary gamma_symbol = child_symbols[2];
				Dictionary label_symbol = child_symbols[3];
				Dictionary parse_symbol = child_symbols[4];
				Dictionary load_symbol = child_symbols[5];
				CHECK_EQ(String(alpha_symbol["name"]), "ALPHA");
				CHECK_EQ(int(alpha_symbol["kind"]), LSP::SymbolKind::EnumMember);
				CHECK_EQ(String(beta_symbol["name"]), "BETA");
				CHECK_EQ(int(beta_symbol["kind"]), LSP::SymbolKind::EnumMember);
				CHECK_EQ(String(gamma_symbol["name"]), "GAMMA");
				CHECK_EQ(int(gamma_symbol["kind"]), LSP::SymbolKind::EnumMember);
				CHECK_EQ(String(label_symbol["name"]), "label");
				CHECK_EQ(int(label_symbol["kind"]), LSP::SymbolKind::Method);
				CHECK_EQ(String(parse_symbol["name"]), "parse");
				CHECK_EQ(int(parse_symbol["kind"]), LSP::SymbolKind::Function);
				CHECK_EQ(String(load_symbol["name"]), "load");
				CHECK_EQ(int(load_symbol["kind"]), LSP::SymbolKind::Function);
			}

			Variant hover_variant = text_document->hover(pos_in(uri, cls.selectionRange.start).to_json());
			REQUIRE(hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary hover = hover_variant;
			Dictionary hover_contents = hover["contents"];
			String hover_value = hover_contents["value"];
			CHECK(hover_value.contains("enum LspGlobalEnum"));
			CHECK(hover_value.contains("Global enum documentation."));

			const LSP::Range label_range = range(pos(10, 6), pos(10, 11));
			const LSP::Range parse_range = range(pos(14, 13), pos(14, 18));
			const LSP::DocumentSymbol *static_reference =
					test_resolve_symbol_at(user_uri, pos(3, 30), uri, "parse", parse_range);
			const LSP::DocumentSymbol *instance_reference =
					test_resolve_symbol_at(user_uri, pos(4, 20), uri, "label", label_range);
			REQUIRE(static_reference);
			REQUIRE(instance_reference);

			Variant static_hover_variant = text_document->hover(pos_in(user_uri, pos(3, 30)).to_json());
			REQUIRE(static_hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary static_hover = static_hover_variant;
			Dictionary static_hover_contents = static_hover["contents"];
			CHECK(String(static_hover_contents["value"]).contains("static func parse(text: String) -> LspGlobalEnum"));
			CHECK(String(static_hover_contents["value"]).contains("Parses a global enum value."));

			Variant instance_hover_variant = text_document->hover(pos_in(user_uri, pos(4, 20)).to_json());
			REQUIRE(instance_hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary instance_hover = instance_hover_variant;
			Dictionary instance_hover_contents = instance_hover["contents"];
			CHECK(String(instance_hover_contents["value"]).contains(R"(func label(prefix: String = "") -> String)"));
			CHECK(String(instance_hover_contents["value"]).contains("Formats a global enum value."));

			Array definitions = text_document->definition(pos_in(user_uri, pos(3, 30)).to_json());
			REQUIRE_EQ(definitions.size(), 1);
			if (definitions.size() == 1) {
				Dictionary definition = definitions[0];
				CHECK_EQ(String(definition["uri"]), uri);
				Dictionary definition_range = definition["range"];
				LSP::Range resolved_definition_range;
				resolved_definition_range.load(definition_range);
				CHECK_EQ(resolved_definition_range, static_reference->selectionRange);
			}

			Dictionary completion_params = pos_in(user_uri, pos(3, 33)).to_json();
			Array completion_items = text_document->completion(completion_params);
			Dictionary parse_completion;
			for (int i = 0; i < completion_items.size(); i++) {
				Dictionary completion = completion_items[i];
				if (String(completion["label"]).begins_with("parse")) {
					parse_completion = completion;
					break;
				}
			}
			REQUIRE(!parse_completion.is_empty());
			Dictionary resolved_parse_completion = text_document->resolve(parse_completion);
			CHECK_EQ(String(resolved_parse_completion["detail"]), "static func parse(text: String) -> LspGlobalEnum");
			Dictionary completion_docs = resolved_parse_completion["documentation"];
			CHECK(String(completion_docs["value"]).contains("Parses a global enum value."));

			LSP::SignatureHelp static_signature_help;
			CHECK_EQ(workspace->resolve_signature(pos_in(user_uri, pos(3, 38)), static_signature_help), OK);
			REQUIRE_EQ(static_signature_help.signatures.size(), 1);
			if (static_signature_help.signatures.size() == 1) {
				const LSP::SignatureInformation &signature = static_signature_help.signatures[0];
				CHECK_EQ(signature.label, "static func parse(text: String) -> LspGlobalEnum");
				REQUIRE_EQ(signature.parameters.size(), 1);
				CHECK_EQ(signature.parameters[0].label, "text: String");
			}

			LSP::SignatureHelp instance_signature_help;
			CHECK_EQ(workspace->resolve_signature(pos_in(user_uri, pos(4, 25)), instance_signature_help), OK);
			REQUIRE_EQ(instance_signature_help.signatures.size(), 1);
			if (instance_signature_help.signatures.size() == 1) {
				const LSP::SignatureInformation &signature = instance_signature_help.signatures[0];
				CHECK_EQ(signature.label, R"(func label(prefix: String = "") -> String)");
				REQUIRE_EQ(signature.parameters.size(), 1);
				CHECK_EQ(signature.parameters[0].label, "prefix: String");
			}
		}

		SUBCASE("A nested enum reports and resolves host functions") {
			String path = "res://lsp/enum_host_functions.fs";
			String uri = workspace->get_file_uri(path);
			assert_no_errors_in(path);

			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			const LSP::DocumentSymbol *status = parser->get_member_symbol("Status");
			REQUIRE(status);
			CHECK_EQ(status->kind, LSP::SymbolKind::Enum);
			REQUIRE_EQ(status->children.size(), 5);
			if (status->children.size() == 5) {
				CHECK_EQ(status->children[0].name, "READY");
				CHECK_EQ(status->children[0].kind, LSP::SymbolKind::EnumMember);
				CHECK_EQ(status->children[1].name, "DONE");
				CHECK_EQ(status->children[1].kind, LSP::SymbolKind::EnumMember);
				CHECK_EQ(status->children[2].name, "label");
				CHECK_EQ(status->children[2].kind, LSP::SymbolKind::Method);
				CHECK_EQ(status->children[2].detail, R"(func label(prefix: String = "") -> String)");
				CHECK_EQ(status->children[3].name, "refresh");
				CHECK_EQ(status->children[3].kind, LSP::SymbolKind::Method);
				CHECK_EQ(status->children[3].detail, "async func refresh() -> String");
				CHECK_EQ(status->children[4].name, "parse");
				CHECK_EQ(status->children[4].kind, LSP::SymbolKind::Function);
				CHECK_EQ(status->children[4].detail, "static func parse(text: String) -> enum_host_functions.fs.Status");
			}

			const LSP::Range parse_range = range(pos(15, 13), pos(15, 18));
			const LSP::Range label_range = range(pos(7, 6), pos(7, 11));
			test_resolve_symbol_at(uri, pos(19, 23), uri, "parse", parse_range);
			test_resolve_symbol_at(uri, pos(20, 21), uri, "label", label_range);

			Ref<FSTextDocument> text_document = proto->get_text_document();
			Variant hover_variant = text_document->hover(pos_in(uri, pos(21, 24)).to_json());
			REQUIRE(hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary hover = hover_variant;
			Dictionary hover_contents = hover["contents"];
			CHECK(String(hover_contents["value"]).contains("async func refresh() -> String"));
			CHECK(String(hover_contents["value"]).contains("Refreshes this status asynchronously."));
		}

		SUBCASE("A namespaced global enum_name resolves hover by declaration identifier") {
			LSPGlobalScriptClassBackup global_class_backup;
			ScriptServer::global_classes_clear();

			String path = "res://lsp/global_enum_namespaced.fs";
			String user_path = "res://lsp/global_enum_namespaced_user.fs";
			String uri = workspace->get_file_uri(path);
			String user_uri = workspace->get_file_uri(user_path);
			StringName language = FSLanguage::get_singleton()->get_name();
			ScriptServer::add_global_class("lsp.enums.LspNamespacedGlobalEnum", String(), language, path, false, false, false, true);
			assert_no_errors_in(path);
			assert_no_errors_in(user_path);

			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			LSP::DocumentSymbol cls = parser->get_symbols();
			CHECK_EQ(cls.name, "lsp.enums.LspNamespacedGlobalEnum");
			CHECK_EQ(cls.kind, LSP::SymbolKind::Enum);
			CHECK_EQ(cls.detail, "enum lsp.enums.LspNamespacedGlobalEnum");
			CHECK(cls.documentation.contains("Namespaced global enum documentation."));

			Ref<FSTextDocument> text_document = proto->get_text_document();
			const LSP::DocumentSymbol *declaration_symbol = workspace->resolve_symbol(pos_in(uri, cls.selectionRange.start));
			REQUIRE(declaration_symbol);
			CHECK_EQ(declaration_symbol->name, "lsp.enums.LspNamespacedGlobalEnum");
			CHECK_EQ(declaration_symbol->kind, LSP::SymbolKind::Enum);

			Variant declaration_hover_variant = text_document->hover(pos_in(uri, cls.selectionRange.start).to_json());
			REQUIRE(declaration_hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary declaration_hover = declaration_hover_variant;
			Dictionary declaration_hover_contents = declaration_hover["contents"];
			String declaration_hover_value = declaration_hover_contents["value"];
			CHECK(declaration_hover_value.contains("enum lsp.enums.LspNamespacedGlobalEnum"));
			CHECK(declaration_hover_value.contains("Namespaced global enum documentation."));

			const LSP::DocumentSymbol *reference_symbol = workspace->resolve_symbol(pos_in(user_uri, pos(2, 25)));
			REQUIRE(reference_symbol);
			CHECK_EQ(reference_symbol->name, "lsp.enums.LspNamespacedGlobalEnum");
			CHECK_EQ(reference_symbol->kind, LSP::SymbolKind::Enum);

			Variant reference_hover_variant = text_document->hover(pos_in(user_uri, pos(2, 25)).to_json());
			REQUIRE(reference_hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary reference_hover = reference_hover_variant;
			Dictionary reference_hover_contents = reference_hover["contents"];
			String reference_hover_value = reference_hover_contents["value"];
			CHECK(reference_hover_value.contains("enum lsp.enums.LspNamespacedGlobalEnum"));
			CHECK(reference_hover_value.contains("Namespaced global enum documentation."));
		}

		SUBCASE("Signature help documents inherited flattened trait method provenance") {
			String path = "res://lsp/trait_signature_help.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);

			LSP::SignatureHelp signature_help;
			Error err = workspace->resolve_signature(pos_in(uri, pos(24, 19)), signature_help);
			CHECK_EQ(err, OK);
			if (err != OK) {
				return;
			}
			REQUIRE(signature_help.signatures.size() == 1);
			if (signature_help.signatures.is_empty()) {
				return;
			}
			const LSP::SignatureInformation &signature = signature_help.signatures[0];
			CHECK_EQ(signature.label, "func describe(amount: int, label: String) -> String");
			CHECK(signature.documentation.value.contains("From trait Damageable"));
		}

		SUBCASE("Signature help keeps base members ahead of subclass traits") {
			String path = "res://lsp/trait_signature_help.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);

			LSP::SignatureHelp signature_help;
			Error err = workspace->resolve_signature(pos_in(uri, pos(25, 31)), signature_help);
			CHECK_EQ(err, OK);
			if (err != OK) {
				return;
			}
			REQUIRE(signature_help.signatures.size() == 1);
			if (signature_help.signatures.is_empty()) {
				return;
			}
			const LSP::SignatureInformation &signature = signature_help.signatures[0];
			CHECK_EQ(signature.label, "func describe_shadow(amount: int) -> int");
			CHECK_FALSE(signature.documentation.value.contains("From trait ShadowDamageable"));
		}

		SUBCASE("Documentation is correctly set") {
			String path = "res://lsp/doc_comments.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			LSP::DocumentSymbol cls = parser->get_symbols();
			REQUIRE(cls.documentation.contains("brief"));
			REQUIRE(cls.documentation.contains("description"));
			REQUIRE(cls.documentation.contains("t1"));
			REQUIRE(cls.documentation.contains("t2"));
			REQUIRE(cls.documentation.contains("t3"));
		}

		SUBCASE("Strict type syntax is preserved in symbols and generated API") {
			String path = "res://lsp/strict_type_presentation.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			Ref<FSTextDocument> text_document = proto->get_text_document();

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

		SUBCASE("Generic method symbols show the type-parameter list and unsubstituted return") {
			String path = "res://lsp/generic_presentation.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);

			const LSP::DocumentSymbol *swap = parser->get_member_symbol("swap");
			REQUIRE(swap);
			CHECK_EQ(swap->detail, "func swap[T](first: T, second: T) -> T");

			const LSP::DocumentSymbol *clamp_within = parser->get_member_symbol("clamp_within");
			REQUIRE(clamp_within);
			CHECK_EQ(clamp_within->detail, "func clamp_within[U: RefCounted](value: U) -> U");

			const LSP::DocumentSymbol *tag = parser->get_member_symbol("tag");
			REQUIRE(tag);
			CHECK_EQ(tag->detail, "func tag[A: Animal](value: A) -> A");
		}

		SUBCASE("Type[T] annotations are preserved across LSP surfaces") {
			String path = "res://lsp/type_metatype_presentation.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			Ref<FSTextDocument> text_document = proto->get_text_document();

			const LSP::DocumentSymbol *user_type = parser->get_member_symbol("user_type");
			REQUIRE(user_type);
			CHECK_EQ(user_type->detail, "var user_type: Type[User] = User");

			const LSP::DocumentSymbol *node_type = parser->get_member_symbol("node_type");
			REQUIRE(node_type);
			CHECK_EQ(node_type->detail, "var node_type: Type[Node] = Node");

			const LSP::DocumentSymbol *int_box_type = parser->get_member_symbol("int_box_type");
			REQUIRE(int_box_type);
			CHECK_EQ(int_box_type->detail, "var int_box_type: Type[Box[int]] = Box[int]");

			const LSP::DocumentSymbol *accept = parser->get_member_symbol("accept");
			REQUIRE(accept);
			CHECK_EQ(accept->detail, "func accept[T](klass: Type[T], value: T) -> T");

			const LSP::DocumentSymbol *id_type = parser->get_member_symbol("id_type");
			REQUIRE(id_type);
			CHECK_EQ(id_type->detail, "func id_type[T](t: Type[T]) -> Type[T]");

			Variant hover_variant = text_document->hover(pos_in(uri, user_type->selectionRange.start).to_json());
			REQUIRE(hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary hover = hover_variant;
			Dictionary hover_contents = hover["contents"];
			CHECK(String(hover_contents["value"]).contains("var user_type: Type[User] = User"));

			const Array &completion_items = parser->get_member_completions();
			Dictionary user_type_completion;
			Dictionary accept_completion;
			for (int i = 0; i < completion_items.size(); i++) {
				Dictionary completion = completion_items[i];
				const String label = completion["label"];
				if (label == "user_type") {
					user_type_completion = completion;
				} else if (label == "accept") {
					accept_completion = completion;
				}
			}
			REQUIRE(!user_type_completion.is_empty());
			REQUIRE(!accept_completion.is_empty());
			Dictionary resolved_user_type_completion = text_document->resolve(user_type_completion);
			Dictionary resolved_accept_completion = text_document->resolve(accept_completion);
			CHECK_EQ(String(resolved_user_type_completion["detail"]), "var user_type: Type[User] = User");
			CHECK_EQ(String(resolved_accept_completion["detail"]), "func accept[T](klass: Type[T], value: T) -> T");

			LSP::SignatureHelp signature_help;
			Error err = workspace->resolve_signature(pos_in(uri, pos(24, 10)), signature_help);
			CHECK_EQ(err, OK);
			if (err != OK) {
				return;
			}
			REQUIRE(signature_help.signatures.size() == 1);
			if (signature_help.signatures.is_empty()) {
				return;
			}
			const LSP::SignatureInformation &signature = signature_help.signatures[0];
			CHECK_EQ(signature.label, "func accept[T](klass: Type[T], value: T) -> T");
			REQUIRE(signature.parameters.size() == 2);
			CHECK_EQ(signature.parameters[0].label, "klass: Type[T]");
			CHECK_EQ(signature.parameters[1].label, "value: T");

			Dictionary api = parser->generate_api();
			Array methods = api["methods"];
			Dictionary accept_api;
			for (int i = 0; i < methods.size(); i++) {
				Dictionary method = methods[i];
				if (String(method["name"]) == "accept") {
					accept_api = method;
					break;
				}
			}
			REQUIRE(!accept_api.is_empty());
			CHECK_EQ(String(accept_api["return_type"]), "T");
			CHECK_EQ(String(accept_api["signature"]), "func accept[T](klass: Type[T], value: T) -> T");
			Array arguments = accept_api["arguments"];
			REQUIRE(arguments.size() == 2);
			Dictionary klass_argument = arguments[0];
			CHECK_EQ(String(klass_argument["type"]), "Type[T]");
		}

		SUBCASE("Type[T] constrained by a trait resolves static trait methods") {
			String path = "res://lsp/type_metatype_trait_constraint.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);

			const LSP::DocumentSymbol *trait_create = workspace->resolve_symbol(pos_in(uri, pos(10, 25)));
			CHECK(trait_create);
			if (trait_create == nullptr) {
				return;
			}
			CHECK_EQ(trait_create->name, "create");
			CHECK_EQ(trait_create->selectionRange.start.line, 1);

			const LSP::DocumentSymbol *concrete_create = workspace->resolve_symbol(pos_in(uri, pos(14, 13)));
			REQUIRE(concrete_create);
			CHECK_EQ(concrete_create->name, "create");
			CHECK_EQ(concrete_create->selectionRange.start.line, 6);
		}

		SUBCASE("Enum default values are shown as constant names") {
			String path = "res://lsp/enum_default_values.fs";
			assert_no_errors_in(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);

			// A property typed with a script enum shows the constant name, not the integer.
			const LSP::DocumentSymbol *mode = parser->get_member_symbol("mode");
			REQUIRE(mode);
			CHECK_EQ(mode->detail, "var mode: enum_default_values.fs.Mode = RUNNING");

			// A property typed with a native enum resolves through the same path.
			const LSP::DocumentSymbol *alignment = parser->get_member_symbol("alignment");
			REQUIRE(alignment);
			CHECK_EQ(alignment->detail, "var alignment: HorizontalAlignment = HORIZONTAL_ALIGNMENT_CENTER");

			// A plain integer property is unaffected.
			const LSP::DocumentSymbol *count = parser->get_member_symbol("count");
			REQUIRE(count);
			CHECK_EQ(count->detail, "var count: int = 3");

			// Enum-typed parameter defaults render as names; plain int defaults are untouched.
			const LSP::DocumentSymbol *set_mode = parser->get_member_symbol("set_mode");
			REQUIRE(set_mode);
			CHECK_EQ(set_mode->detail, "func set_mode(target: enum_default_values.fs.Mode = IDLE, repeats: int = 2) -> void");
		}

		SUBCASE("AsyncCallable types are presented distinctly across LSP surfaces") {
			String path = "res://lsp/async_callable_presentation.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			Ref<FSTextDocument> text_document = proto->get_text_document();

			// Member symbols carry the AsyncCallable rendering, not a plain Callable.
			const LSP::DocumentSymbol *on_ready = parser->get_member_symbol("on_ready");
			REQUIRE(on_ready);
			CHECK_EQ(on_ready->detail, "var on_ready: AsyncCallable[[int], String]");

			const LSP::DocumentSymbol *fetch = parser->get_member_symbol("fetch");
			REQUIRE(fetch);
			CHECK_EQ(fetch->detail, "async func fetch(handler: AsyncCallable[[int], String]) -> String");

			// Hover preserves the AsyncCallable type.
			Variant hover_variant = text_document->hover(pos_in(uri, on_ready->selectionRange.start).to_json());
			REQUIRE(hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary hover = hover_variant;
			Dictionary hover_contents = hover["contents"];
			CHECK(String(hover_contents["value"]).contains("var on_ready: AsyncCallable[[int], String]"));

			// Resolved completion details keep the AsyncCallable rendering.
			const Array &completion_items = parser->get_member_completions();
			Dictionary on_ready_completion;
			Dictionary fetch_completion;
			for (int i = 0; i < completion_items.size(); i++) {
				Dictionary completion = completion_items[i];
				const String label = completion["label"];
				if (label == "on_ready") {
					on_ready_completion = completion;
				} else if (label == "fetch") {
					fetch_completion = completion;
				}
			}
			REQUIRE(!on_ready_completion.is_empty());
			REQUIRE(!fetch_completion.is_empty());
			Dictionary resolved_on_ready_completion = text_document->resolve(on_ready_completion);
			Dictionary resolved_fetch_completion = text_document->resolve(fetch_completion);
			CHECK_EQ(String(resolved_on_ready_completion["detail"]), "var on_ready: AsyncCallable[[int], String]");
			CHECK_EQ(String(resolved_fetch_completion["detail"]), "async func fetch(handler: AsyncCallable[[int], String]) -> String");

			// Signature help renders the async function and its AsyncCallable parameter.
			LSP::SignatureHelp signature_help;
			CHECK_EQ(workspace->resolve_signature(pos_in(uri, pos(8, 8)), signature_help), OK);
			REQUIRE(signature_help.signatures.size() == 1);
			const LSP::SignatureInformation &signature = signature_help.signatures[0];
			CHECK_EQ(signature.label, "async func fetch(handler: AsyncCallable[[int], String]) -> String");
			REQUIRE(signature.parameters.size() == 1);
			CHECK_EQ(signature.parameters[0].label, "handler: AsyncCallable[[int], String]");
		}

		SUBCASE("Coroutine types are presented distinctly across LSP surfaces") {
			String path = "res://lsp/coroutine_presentation.fs";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			Ref<FSTextDocument> text_document = proto->get_text_document();

			// Member symbols carry the Coroutine[T] rendering, not the native FSFunctionState.
			const LSP::DocumentSymbol *pending = parser->get_member_symbol("pending");
			REQUIRE(pending);
			CHECK_EQ(pending->detail, "var pending: Coroutine[String]");

			const LSP::DocumentSymbol *schedule = parser->get_member_symbol("schedule");
			REQUIRE(schedule);
			CHECK_EQ(schedule->detail, "func schedule(work: Coroutine[String]) -> void");

			// Hover preserves the Coroutine[T] type.
			Variant hover_variant = text_document->hover(pos_in(uri, pending->selectionRange.start).to_json());
			REQUIRE(hover_variant.get_type() == Variant::DICTIONARY);
			Dictionary hover = hover_variant;
			Dictionary hover_contents = hover["contents"];
			CHECK(String(hover_contents["value"]).contains("var pending: Coroutine[String]"));

			// Resolved completion details keep the Coroutine[T] rendering.
			const Array &completion_items = parser->get_member_completions();
			Dictionary pending_completion;
			Dictionary schedule_completion;
			for (int i = 0; i < completion_items.size(); i++) {
				Dictionary completion = completion_items[i];
				const String label = completion["label"];
				if (label == "pending") {
					pending_completion = completion;
				} else if (label == "schedule") {
					schedule_completion = completion;
				}
			}
			REQUIRE(!pending_completion.is_empty());
			REQUIRE(!schedule_completion.is_empty());
			Dictionary resolved_pending_completion = text_document->resolve(pending_completion);
			Dictionary resolved_schedule_completion = text_document->resolve(schedule_completion);
			CHECK_EQ(String(resolved_pending_completion["detail"]), "var pending: Coroutine[String]");
			CHECK_EQ(String(resolved_schedule_completion["detail"]), "func schedule(work: Coroutine[String]) -> void");

			// Signature help renders the Coroutine[T] parameter.
			LSP::SignatureHelp signature_help;
			CHECK_EQ(workspace->resolve_signature(pos_in(uri, pos(12, 11)), signature_help), OK);
			REQUIRE(signature_help.signatures.size() == 1);
			const LSP::SignatureInformation &signature = signature_help.signatures[0];
			CHECK_EQ(signature.label, "func schedule(work: Coroutine[String]) -> void");
			REQUIRE(signature.parameters.size() == 1);
			CHECK_EQ(signature.parameters[0].label, "work: Coroutine[String]");
		}

		memdelete(proto);
		memdelete(efs);
	}

	TEST_CASE("[textDocument][definition] resolves FoundryScript namespaces") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		LSPGlobalScriptClassBackup global_class_backup;
		register_lsp_namespace_global_classes();

		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_user.fs");
		const String base_uri = workspace->get_file_uri("res://lsp/namespace_lsp_base.fs");
		const String controller_uri = workspace->get_file_uri("res://lsp/namespace_lsp_controller.fs");

		assert_no_errors_in("res://lsp/namespace_lsp_user.fs");

		const LSP::Range base_selection = range(pos(1, 11), pos(1, 27));
		test_resolve_symbol_at(user_uri, pos(5, 21), base_uri, "LspBaseCharacter", base_selection);
		test_resolve_symbol_at(user_uri, pos(7, 37), base_uri, "LspBaseCharacter", base_selection);
		test_resolve_symbol_at(user_uri, pos(10, 14), base_uri, "LspBaseCharacter", base_selection);
		test_resolve_symbol_at(user_uri, pos(11, 34), base_uri, "LspBaseCharacter", base_selection);

		test_resolve_symbol_at(user_uri, pos(6, 33), controller_uri, "LspMyCharacterController", range(pos(1, 11), pos(1, 35)));

		memdelete(proto);
		memdelete(efs);
	}

	TEST_CASE("[textDocument][definition] leaves ambiguous FoundryScript namespace imports unresolved") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		LSPGlobalScriptClassBackup global_class_backup;
		register_lsp_namespace_global_classes();

		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String namespace_user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_user.fs");
		const String base_uri = workspace->get_file_uri("res://lsp/namespace_lsp_base.fs");
		const String user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_ambiguous_user.fs");

		assert_no_errors_in("res://lsp/namespace_lsp_user.fs");
		test_resolve_symbol_at(namespace_user_uri, pos(5, 21), base_uri, "LspBaseCharacter", range(pos(1, 11), pos(1, 27)));

		const LSP::DocumentSymbol *symbol = workspace->resolve_symbol(pos_in(user_uri, pos(6, 16)));
		CHECK_FALSE(symbol);

		memdelete(proto);
		memdelete(efs);
	}

	TEST_CASE("[textDocument][rename] updates FoundryScript namespace references") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		LSPGlobalScriptClassBackup global_class_backup;
		register_lsp_namespace_global_classes();

		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		Ref<FSTextDocument> text_document = proto->get_text_document();
		const String user_uri = workspace->get_file_uri("res://lsp/namespace_lsp_user.fs");
		const String base_uri = workspace->get_file_uri("res://lsp/namespace_lsp_base.fs");
		const String controller_uri = workspace->get_file_uri("res://lsp/namespace_lsp_controller.fs");

		assert_no_errors_in("res://lsp/namespace_lsp_user.fs");

		Dictionary edit = text_document->rename(make_rename_params(base_uri, pos(1, 12), "RenamedBaseCharacter"));

		const Array base_edits = workspace_edits_for_uri(edit, base_uri);
		const Array user_edits = workspace_edits_for_uri(edit, user_uri);
		CHECK_EQ(base_edits.size(), 1);
		CHECK(text_edits_include(base_edits, 1, 11, 27, "RenamedBaseCharacter"));
		CHECK_EQ(user_edits.size(), 4);
		CHECK(text_edits_include(user_edits, 5, 20, 36, "RenamedBaseCharacter"));
		CHECK(text_edits_include(user_edits, 7, 36, 52, "RenamedBaseCharacter"));
		CHECK(text_edits_include(user_edits, 10, 13, 29, "RenamedBaseCharacter"));
		CHECK(text_edits_include(user_edits, 11, 33, 49, "RenamedBaseCharacter"));
		Dictionary changes = edit["changes"];
		CHECK_FALSE(changes.has(controller_uri));

		memdelete(proto);
		memdelete(efs);
	}

	TEST_CASE("[textDocument][definition] resolves trait references") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);

		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String uri = workspace->get_file_uri("res://lsp/traits.fs");

		assert_no_errors_in("res://lsp/traits.fs");

		const LSP::Range trait_selection = range(pos(2, 6), pos(2, 14));
		// Reference in a `uses` clause.
		test_resolve_symbol_at(uri, pos(7, 8), uri, "Drawable", trait_selection);
		// Reference in a type position.
		test_resolve_symbol_at(uri, pos(9, 20), uri, "Drawable", trait_selection);

		memdelete(proto);
		memdelete(efs);
	}

	TEST_CASE("[textDocument][definition] resolves custom annotation references") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);

		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String uri = workspace->get_file_uri("res://lsp/annotations.fs");

		assert_no_errors_in("res://lsp/annotations.fs");

		const LSP::Range marker_selection = range(pos(2, 11), pos(2, 20));
		const LSP::Range timeout_selection = range(pos(3, 11), pos(3, 21));
		// Marker annotation usage resolves to its declaration.
		test_resolve_symbol_at(uri, pos(5, 5), uri, "my_marker", marker_selection);
		// Parameterized annotation usage resolves to its declaration.
		test_resolve_symbol_at(uri, pos(6, 5), uri, "my_timeout", timeout_selection);

		// An annotation-only file whose declaration is on the first line still resolves to the
		// declaration symbol rather than the script root.
		const String library_uri = workspace->get_file_uri("res://lsp/annotation_library.fs");
		assert_no_errors_in("res://lsp/annotation_library.fs");
		test_resolve_symbol_at(library_uri, pos(2, 2), library_uri, "tag", range(pos(0, 11), pos(0, 14)));

		// A custom annotation whose name collides with a class resolves to the declaration, not the
		// class, because annotation names live in a separate symbol space.
		const String collision_uri = workspace->get_file_uri("res://lsp/annotation_collision.fs");
		assert_no_errors_in("res://lsp/annotation_collision.fs");
		test_resolve_symbol_at(collision_uri, pos(4, 3), collision_uri, "Node", range(pos(2, 11), pos(2, 15)));

		memdelete(proto);
		memdelete(efs);
	}

	TEST_CASE("[textDocument][rename] updates trait declarations and uses references") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);

		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		Ref<FSTextDocument> text_document = proto->get_text_document();
		const String uri = workspace->get_file_uri("res://lsp/traits.fs");

		assert_no_errors_in("res://lsp/traits.fs");

		Dictionary edit = text_document->rename(make_rename_params(uri, pos(2, 8), "Renderable"));
		const Array edits = workspace_edits_for_uri(edit, uri);
		CHECK_EQ(edits.size(), 3);
		// Declaration.
		CHECK(text_edits_include(edits, 2, 6, 14, "Renderable"));
		// `uses` reference.
		CHECK(text_edits_include(edits, 7, 6, 14, "Renderable"));
		// Type-position reference.
		CHECK(text_edits_include(edits, 9, 18, 26, "Renderable"));

		memdelete(proto);
		memdelete(efs);
	}

	TEST_CASE("[textDocument][codeAction] exposes refactors") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		Ref<FSTextDocument> text_document = proto->get_text_document();

		SUBCASE("server capabilities advertise code actions") {
			TestFSLanguageProtocolInitializer::mark_initialized(proto);

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
			const String uri = workspace->get_file_uri("res://lsp/code_action_type_annotation.fs");
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
			const String uri = workspace->get_file_uri("res://lsp/code_action_stale.fs");
			text_document->didOpen(make_did_open_params(uri, "var score = 1\n"));

			Array actions = text_document->codeAction(make_code_action_params(uri, range(pos(0, 1), pos(0, 1))));
			Dictionary action = first_code_action_with_kind(actions, "refactor.rewrite");
			REQUIRE_FALSE(action.is_empty());

			text_document->didChange(make_did_change_params(uri, "var score: int = 1\n"));
			Dictionary resolved = text_document->resolveCodeAction(action);
			CHECK_FALSE(resolved.has("edit"));

			Array notifications = TestFSLanguageProtocolInitializer::take_client_notifications(
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
			const String uri = workspace->get_file_uri("res://refactor/rename_local.fs");

			Array actions = text_document->codeAction(make_code_action_params(uri, range(pos(3, 5), pos(3, 5))));
			Dictionary action = first_code_action_with_kind(actions, "refactor.rename");
			CHECK(action.is_empty());
		}

		SUBCASE("does not list Override Method as a generic code action") {
			const String source =
					"extends Control\n"
					"\n"
					"var marker := 0\n";
			const String uri = workspace->get_file_uri("res://lsp/code_action_override_method.fs");
			text_document->didOpen(make_did_open_params(uri, source));

			Array actions = text_document->codeAction(make_code_action_params(uri, range(pos(2, 1), pos(2, 1))));
			for (int i = 0; i < actions.size(); i++) {
				Dictionary action = actions[i];
				CHECK_NE(String(action["title"]), "Override Method...");
			}
		}

		SUBCASE("filters code actions by requested kind") {
			const String source = "var score = 1\n";
			const String uri = workspace->get_file_uri("res://lsp/code_action_filter.fs");
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
			FSTests::assert_no_errors_in("res://refactor/rename_cross_file_user.fs");

			const String target_uri = workspace->get_file_uri("res://refactor/rename_cross_file_target.fs");
			const String user_uri = workspace->get_file_uri("res://refactor/rename_cross_file_user.fs");
			Dictionary edit = text_document->rename(make_rename_params(target_uri, pos(2, 5), "renamed_count"));

			CHECK_EQ(workspace_edits_for_uri(edit, target_uri).size(), 3);
			CHECK_EQ(workspace_edits_for_uri(edit, user_uri).size(), 2);
		}

		SUBCASE("textDocument rename returns exported cross-file script edits") {
			FSTests::assert_no_errors_in("res://refactor/rename_cross_file_exported_user.fs");

			const String target_uri = workspace->get_file_uri("res://refactor/rename_cross_file_exported_target.fs");
			const String user_uri = workspace->get_file_uri("res://refactor/rename_cross_file_exported_user.fs");
			const String scene_uri = workspace->get_file_uri("res://refactor/rename_cross_file_exported_scene.tscn");
			Dictionary rename_params = make_rename_params(target_uri, pos(2, 13), "renamed_exported_count");
			Dictionary edit = text_document->rename(rename_params);

			CHECK_EQ(workspace_edits_for_uri(edit, target_uri).size(), 3);
			CHECK_EQ(workspace_edits_for_uri(edit, user_uri).size(), 2);
			Dictionary changes = edit["changes"];
			CHECK_FALSE(changes.has(scene_uri));

			Array notifications = TestFSLanguageProtocolInitializer::take_client_notifications(
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
	}

	TEST_CASE("[textDocument][formatting] formats through the canonical core") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		Ref<FSTextDocument> text_document = proto->get_text_document();

		SUBCASE("server capabilities advertise document formatting") {
			TestFSLanguageProtocolInitializer::mark_initialized(proto);

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
			CHECK(bool(capabilities["documentFormattingProvider"]));
		}

		SUBCASE("returns a single whole-document edit with canonical text") {
			const String source = "func f():\n\treturn  1\n";
			const String uri = workspace->get_file_uri("res://lsp/formatting_basic.fs");
			text_document->didOpen(make_did_open_params(uri, source));

			Array edits = text_document->formatting(make_formatting_params(uri));
			REQUIRE_EQ(edits.size(), 1);
			Dictionary edit = edits[0];
			Dictionary range = edit["range"];
			Dictionary start = range["start"];
			Dictionary end = range["end"];
			CHECK_EQ(int(start["line"]), 0);
			CHECK_EQ(int(start["character"]), 0);
			// Two lines plus a trailing newline: the end sits at the start of line 2.
			CHECK_EQ(int(end["line"]), 2);
			CHECK_EQ(int(end["character"]), 0);

			FSFormatter formatter;
			FSFormatter::Result expected;
			REQUIRE_EQ(formatter.format(source, "formatting_basic.fs", expected), OK);
			CHECK_EQ(String(edit["newText"]), expected.formatted);
		}

		SUBCASE("returns no edits when the document is already canonical") {
			FSFormatter formatter;
			FSFormatter::Result canonical;
			REQUIRE_EQ(formatter.format("func f():\n\treturn 1\n", "x.fs", canonical), OK);

			const String uri = workspace->get_file_uri("res://lsp/formatting_canonical.fs");
			text_document->didOpen(make_did_open_params(uri, canonical.formatted));

			Array edits = text_document->formatting(make_formatting_params(uri));
			CHECK_EQ(edits.size(), 0);
		}

		SUBCASE("returns no edits when the document does not parse") {
			const String uri = workspace->get_file_uri("res://lsp/formatting_invalid.fs");
			text_document->didOpen(make_did_open_params(uri, "func f(:\n"));

			Array edits = text_document->formatting(make_formatting_params(uri));
			CHECK_EQ(edits.size(), 0);
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Blocked pre-compile gate suppresses initial workspace script reload and publishes build diagnostics") {
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String generated_path = "res://lsp/pre_compile_blocked_generated.fs";
		ScopedLSPTempFile generated(generated_path, "class_name PreCompileBlockedGenerated\n");

		workspace->set_build_pipeline_status_override_for_tests(
				make_blocked_pre_compile_snapshot("pre_compile failed before generated sources were ready"));
		CHECK_EQ(workspace->initialize(), OK);

		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) == nullptr);

		Array notifications = TestFSLanguageProtocolInitializer::take_client_notifications(
				proto, "textDocument/publishDiagnostics");
		bool saw_build_diagnostic = false;
		for (int i = 0; i < notifications.size(); i++) {
			Dictionary notification = notifications[i];
			Dictionary params = notification["params"];
			Array diagnostics = params["diagnostics"];
			for (int j = 0; j < diagnostics.size(); j++) {
				Dictionary diagnostic = diagnostics[j];
				if (String(diagnostic.get("message", "")).contains("pre_compile failed")) {
					saw_build_diagnostic = true;
				}
			}
		}
		CHECK(saw_build_diagnostic);

		workspace->clear_build_pipeline_status_override_for_tests();
		ERR_PRINT_OFF;
		CHECK_EQ(workspace->initialize(), OK);
		ERR_PRINT_ON;
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		notifications = TestFSLanguageProtocolInitializer::take_client_notifications(
				proto, "textDocument/publishDiagnostics");
		bool saw_build_diagnostic_clear = false;
		for (int i = 0; i < notifications.size(); i++) {
			Dictionary notification = notifications[i];
			Dictionary params = notification["params"];
			if (String(params.get("uri", "")).ends_with("/project.foundry")) {
				Array diagnostics = params["diagnostics"];
				if (diagnostics.is_empty()) {
					saw_build_diagnostic_clear = true;
				}
			}
		}
		CHECK(saw_build_diagnostic_clear);

		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Language protocol retries workspace initialization after blocked pre-compile gate") {
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String generated_path = "res://lsp/pre_compile_protocol_retry_generated.fs";
		ScopedLSPTempFile generated(generated_path, "class_name PreCompileProtocolRetryGenerated\n");

		Dictionary params;
		params["rootPath"] = workspace->root;

		workspace->set_build_pipeline_status_override_for_tests(
				make_blocked_pre_compile_snapshot("pre_compile failed before protocol initialization"));
		proto->call("initialize", params);
		CHECK_FALSE(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) == nullptr);

		workspace->clear_build_pipeline_status_override_for_tests();
		ERR_PRINT_OFF;
		proto->call("initialize", params);
		ERR_PRINT_ON;
		CHECK(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Publishing diagnostics completes initialization after cleared pre-compile gate") {
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String script_path = "res://lsp/pre_compile_publish_recovery.fs";
		ScopedLSPTempFile script(script_path, "class_name PreCompilePublishRecovery\n");

		workspace->set_build_pipeline_status_override_for_tests(
				make_blocked_pre_compile_snapshot("pre_compile blocked before diagnostics recovery"));
		CHECK_EQ(workspace->initialize(), OK);
		CHECK_FALSE(workspace->is_initialized());
		CHECK_FALSE(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) == nullptr);

		workspace->clear_build_pipeline_status_override_for_tests();
		ERR_PRINT_OFF;
		workspace->publish_diagnostics(script_path);
		ERR_PRINT_ON;

		CHECK(workspace->is_initialized());
		CHECK(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) != nullptr);

		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Disabled build pipeline ignores stale task sections during workspace indexing") {
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();

		const String script_path = "res://lsp/pre_compile_disabled_config_indexed.fs";
		const String config_text =
				"[build]\n"
				"enabled=false\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"missing_provider\"\n"
				"outputs=PackedStringArray(\"res://lsp/pre_compile_disabled_output.fs\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		ScopedLSPTempFile script(script_path, "class_name PreCompileDisabledConfigIndexed\n");

		ERR_PRINT_OFF;
		CHECK_EQ(workspace->initialize(), OK);
		ERR_PRINT_ON;
		CHECK(workspace->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) != nullptr);

		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Post-compile config errors do not block workspace indexing") {
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();

		const String script_path = "res://lsp/post_compile_invalid_config_indexed.fs";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"post_compile=PackedStringArray(\"bundle\")\n"
				"\n"
				"[build/tasks/bundle]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		ScopedLSPTempFile script(script_path, "class_name PostCompileInvalidConfigIndexed\n");

		ERR_PRINT_OFF;
		CHECK_EQ(workspace->initialize(), OK);
		ERR_PRINT_ON;
		CHECK(workspace->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) != nullptr);

		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Post-compile provider load failure does not stop pre-compile recovery") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String generated_path = "res://lsp/pre_compile_post_provider_failure_generated.fs";
		const String provider_path = "res://lsp/post_compile_bad_provider.fs";
		const String generator_script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompilePostProviderFailureGenerated\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"post_compile=PackedStringArray(\"bundle\")\n"
				"\n"
				"[build/providers/post_provider]\n"
				"script=\"" +
				provider_path + "\"\n"
								"class_name=\"PostProvider\"\n"
								"\n"
								"[build/tasks/generate]\n"
								"provider=\"command\"\n"
								"command=\"python3\"\n"
								"args=PackedStringArray(\"-c\", " +
				Variant(generator_script).to_json_string() + ", \"" + generated_path + "\")\n"
																					   "outputs=PackedStringArray(\"" +
				generated_path + "\")\n"
								 "\n"
								 "[build/tasks/bundle]\n"
								 "provider=\"post_provider\"\n"
								 "outputs=PackedStringArray(\"res://lsp/post_compile_provider_output.txt\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		ScopedLSPTempFile provider_script(provider_path, "class_name PostProvider\nfunc run(:\n");

		ERR_PRINT_OFF;
		CHECK_EQ(proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		CHECK(FileAccess::exists(ScopedLSPTempFile::resolve_path(generated_path)));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path(generated_path));
		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg"));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Post-compile fingerprint probes do not run during workspace initialization") {
		FSLanguageProtocol *proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String marker_path = "res://lsp/post_compile_init_tool_version_ran.txt";
		const String script_path = "res://lsp/post_compile_probe_target.fs";
		const String tool_version_script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('ran\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"post_compile=PackedStringArray(\"bundle\")\n"
				"\n"
				"[build/tasks/bundle]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", \"print('bundle')\")\n"
				"outputs=PackedStringArray(\"res://lsp/post_compile_probe_output.txt\")\n"
				"tool_version_command=PackedStringArray(\"python3\", \"-c\", " +
				Variant(tool_version_script).to_json_string() + ", \"" + marker_path + "\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		ScopedLSPTempFile target(script_path, "class_name PostCompileProbeTarget\n");

		ERR_PRINT_OFF;
		CHECK_EQ(proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) != nullptr);
		CHECK_FALSE(FileAccess::exists(ScopedLSPTempFile::resolve_path(marker_path)));

		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path(marker_path));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Successful generated Foundry Script outputs refresh filesystem and workspace index") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		workspace->set_build_pipeline_status_override_for_tests(make_clean_pre_compile_snapshot());
		ERR_PRINT_OFF;
		CHECK_EQ(workspace->initialize(), OK);
		ERR_PRINT_ON;

		const int before_scan_count = EditorFileSystem::get_singleton()->get_scan_changes_call_count_for_tests();
		const String generated_path = "res://lsp/pre_compile_success_generated.fs";
		ScopedLSPTempFile generated(generated_path, "class_name PreCompileSuccessGenerated\n");

		PackedStringArray outputs;
		outputs.push_back(generated_path);
		ERR_PRINT_OFF;
		CHECK(workspace->refresh_after_successful_build_outputs(outputs));
		ERR_PRINT_ON;

		CHECK_GT(EditorFileSystem::get_singleton()->get_scan_changes_call_count_for_tests(), before_scan_count);
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		workspace->clear_build_pipeline_status_override_for_tests();
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Successful non-script output recovers blocked initial workspace index") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String script_path = "res://lsp/pre_compile_non_script_recovery.fs";
		ScopedLSPTempFile script(script_path, "class_name PreCompileNonScriptRecovery\n");

		workspace->set_build_pipeline_status_override_for_tests(
				make_blocked_pre_compile_snapshot("pre_compile non-script output was not ready"));
		CHECK_EQ(workspace->initialize(), OK);
		CHECK_FALSE(workspace->is_initialized());
		CHECK_FALSE(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) == nullptr);

		workspace->clear_build_pipeline_status_override_for_tests();
		PackedStringArray outputs;
		outputs.push_back("res://lsp/pre_compile_non_script_recovery.txt");
		ERR_PRINT_OFF;
		CHECK(workspace->refresh_after_successful_build_outputs(outputs));
		ERR_PRINT_ON;
		CHECK(workspace->is_initialized());
		CHECK(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) != nullptr);

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Successful output refresh keeps initial index blocked while build gate remains blocked") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		const String script_path = "res://lsp/pre_compile_still_blocked_refresh.fs";
		ScopedLSPTempFile script(script_path, "class_name PreCompileStillBlockedRefresh\n");

		workspace->set_build_pipeline_status_override_for_tests(
				make_blocked_pre_compile_snapshot("pre_compile still blocks refresh recovery"));
		CHECK_EQ(workspace->initialize(), OK);
		CHECK_FALSE(workspace->is_initialized());
		CHECK_FALSE(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) == nullptr);

		PackedStringArray outputs;
		outputs.push_back("res://lsp/pre_compile_still_blocked_refresh.txt");
		CHECK(workspace->refresh_after_successful_build_outputs(outputs));
		CHECK_FALSE(workspace->is_initialized());
		CHECK_FALSE(proto->is_initialized());
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(script_path) == nullptr);

		workspace->clear_build_pipeline_status_override_for_tests();
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Successful dotted directory output refreshes workspace scripts") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		workspace->set_build_pipeline_status_override_for_tests(make_clean_pre_compile_snapshot());
		ERR_PRINT_OFF;
		CHECK_EQ(workspace->initialize(), OK);
		ERR_PRINT_ON;

		const String generated_path = "res://lsp/generated.v2/pre_compile_dotted_directory_generated.fs";
		ScopedLSPTempFile generated(generated_path, "class_name PreCompileDottedDirectoryGenerated\n");

		PackedStringArray outputs;
		outputs.push_back("res://lsp/generated.v2");
		ERR_PRINT_OFF;
		CHECK(workspace->refresh_after_successful_build_outputs(outputs));
		ERR_PRINT_ON;

		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		workspace->clear_build_pipeline_status_override_for_tests();
		ScopedLSPTempFile::remove_recursive(ScopedLSPTempFile::resolve_path("res://lsp/generated.v2"));
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Publishing diagnostics does not run command fingerprint probes") {
		FSLanguageProtocol *proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String marker_path = "res://lsp/pre_compile_publish_tool_version_ran.txt";
		const String target_path = "res://lsp/pre_compile_publish_target.fs";
		const String tool_version_script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('ran\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", \"print('generate')\")\n"
				"outputs=PackedStringArray(\"res://lsp/pre_compile_publish_output.txt\")\n"
				"tool_version_command=PackedStringArray(\"python3\", \"-c\", " +
				Variant(tool_version_script).to_json_string() + ", \"" + marker_path + "\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		ScopedLSPTempFile target(target_path, "class_name PreCompilePublishTarget\n");

		ERR_PRINT_OFF;
		proto->get_workspace()->publish_diagnostics(target_path);
		ERR_PRINT_ON;

		CHECK_FALSE(FileAccess::exists(ScopedLSPTempFile::resolve_path(marker_path)));

		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Missing pre-compile command output reruns before workspace indexing") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String generated_path = "res://lsp/pre_compile_missing_output_generated.fs";
		const String script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompileMissingOutputGenerated\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(script).to_json_string() + ", \"" + generated_path + "\")\n"
																			 "outputs=PackedStringArray(\"" +
				generated_path + "\")\n";

		FSLanguageProtocol *first_proto = initialize(root);
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		const String absolute_generated_path = ScopedLSPTempFile::resolve_path(generated_path);

		ERR_PRINT_OFF;
		CHECK_EQ(first_proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;
		CHECK(FileAccess::exists(absolute_generated_path));
		memdelete(first_proto);
		finish_language();

		DirAccess::remove_absolute(absolute_generated_path);
		CHECK_FALSE(FileAccess::exists(absolute_generated_path));

		FSLanguageProtocol *second_proto = initialize(root);
		ERR_PRINT_OFF;
		CHECK_EQ(second_proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;
		CHECK(FileAccess::exists(absolute_generated_path));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		DirAccess::remove_absolute(absolute_generated_path);
		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg"));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(second_proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Clean pre-compile command stays skipped when another command reruns") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String counter_path = "res://lsp/pre_compile_clean_counter.txt";
		const String generated_path = "res://lsp/pre_compile_skip_clean_generated.fs";
		const String counter_script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"value = 0\n"
				"if path.exists():\n"
				"    value = int(path.read_text().strip() or '0')\n"
				"path.write_text(str(value + 1))\n";
		const String generator_script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompileSkipCleanGenerated\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"stable\", \"generate\")\n"
				"\n"
				"[build/tasks/stable]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(counter_script).to_json_string() + ", \"" + counter_path + "\")\n"
																				   "outputs=PackedStringArray(\"" +
				counter_path + "\")\n"
							   "\n"
							   "[build/tasks/generate]\n"
							   "provider=\"command\"\n"
							   "command=\"python3\"\n"
							   "args=PackedStringArray(\"-c\", " +
				Variant(generator_script).to_json_string() + ", \"" + generated_path + "\")\n"
																					   "outputs=PackedStringArray(\"" +
				generated_path + "\")\n";

		FSLanguageProtocol *first_proto = initialize(root);
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		const String absolute_counter_path = ScopedLSPTempFile::resolve_path(counter_path);
		const String absolute_generated_path = ScopedLSPTempFile::resolve_path(generated_path);

		ERR_PRINT_OFF;
		CHECK_EQ(first_proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		Ref<FileAccess> counter = FileAccess::open(absolute_counter_path, FileAccess::READ);
		REQUIRE(counter.is_valid());
		CHECK_EQ(counter->get_as_utf8_string(), "1");
		CHECK(FileAccess::exists(absolute_generated_path));
		memdelete(first_proto);
		finish_language();

		DirAccess::remove_absolute(absolute_generated_path);
		CHECK_FALSE(FileAccess::exists(absolute_generated_path));

		FSLanguageProtocol *second_proto = initialize(root);
		ERR_PRINT_OFF;
		CHECK_EQ(second_proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		counter = FileAccess::open(absolute_counter_path, FileAccess::READ);
		REQUIRE(counter.is_valid());
		CHECK_EQ(counter->get_as_utf8_string(), "1");
		CHECK(FileAccess::exists(absolute_generated_path));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		DirAccess::remove_absolute(absolute_counter_path);
		DirAccess::remove_absolute(absolute_generated_path);
		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg"));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(second_proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Post-compile failure does not suppress runnable pre-compile indexing") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String generated_path = "res://lsp/pre_compile_post_failure_generated.fs";
		const String post_output_path = "res://lsp/post_compile_failed_output.txt";
		const String script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompilePostFailureGenerated\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"post_compile=PackedStringArray(\"bundle\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(script).to_json_string() + ", \"" + generated_path + "\")\n"
																			 "outputs=PackedStringArray(\"" +
				generated_path + "\")\n"
								 "\n"
								 "[build/tasks/bundle]\n"
								 "provider=\"command\"\n"
								 "command=\"python3\"\n"
								 "args=PackedStringArray(\"-c\", \"print('post')\")\n"
								 "outputs=PackedStringArray(\"" +
				post_output_path + "\")\n";

		FSLanguageProtocol *proto = initialize(root);
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		PackedStringArray post_outputs;
		post_outputs.push_back(post_output_path);
		ProjectBuildState state;
		state.record_task_result("bundle", "failed-fingerprint", post_outputs, false);
		CHECK_EQ(state.save(), OK);

		ERR_PRINT_OFF;
		CHECK_EQ(proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		CHECK(FileAccess::exists(ScopedLSPTempFile::resolve_path(generated_path)));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path(generated_path));
		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg"));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Failed later pre-compile command keeps earlier generated scripts unindexed") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String generated_path = "res://lsp/pre_compile_partial_failure_generated.fs";
		const String generator_script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompilePartialFailureGenerated\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\", \"fail\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(generator_script).to_json_string() + ", \"" + generated_path + "\")\n"
																					   "outputs=PackedStringArray(\"" +
				generated_path + "\")\n"
								 "\n"
								 "[build/tasks/fail]\n"
								 "provider=\"command\"\n"
								 "command=\"python3\"\n"
								 "args=PackedStringArray(\"-c\", \"import sys; sys.exit(3)\")\n"
								 "outputs=PackedStringArray(\"res://lsp/pre_compile_partial_failure_marker.txt\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);

		ERR_PRINT_OFF;
		CHECK_EQ(proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		CHECK(FileAccess::exists(ScopedLSPTempFile::resolve_path(generated_path)));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) == nullptr);

		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path(generated_path));
		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg"));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Pre-compile build state save failure blocks workspace indexing") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String generated_path = "res://lsp/pre_compile_state_save_failure_generated.fs";
		const String generator_script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompileStateSaveFailureGenerated\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(generator_script).to_json_string() + ", \"" + generated_path + "\")\n"
																					   "outputs=PackedStringArray(\"" +
				generated_path + "\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		const String build_state_path = ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg");
		ScopedLSPTempFile::remove_recursive(build_state_path);
		DirAccess::remove_absolute(build_state_path);
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(build_state_path), OK);

		ERR_PRINT_OFF;
		CHECK_EQ(proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		CHECK(FileAccess::exists(ScopedLSPTempFile::resolve_path(generated_path)));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) == nullptr);

		Array notifications = TestFSLanguageProtocolInitializer::take_client_notifications(
				proto, "textDocument/publishDiagnostics");
		bool saw_persist_diagnostic = false;
		for (int i = 0; i < notifications.size(); i++) {
			Dictionary notification = notifications[i];
			Dictionary params = notification["params"];
			Array diagnostics = params["diagnostics"];
			for (int j = 0; j < diagnostics.size(); j++) {
				Dictionary diagnostic = diagnostics[j];
				if (String(diagnostic.get("message", "")).contains("could not be persisted")) {
					saw_persist_diagnostic = true;
				}
			}
		}
		CHECK(saw_persist_diagnostic);

		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path(generated_path));
		ScopedLSPTempFile::remove_recursive(build_state_path);
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Dirty pre-compile command runs before initial workspace script reload") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String generated_path = "res://lsp/pre_compile_run_generated.fs";
		const String script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompileRunGenerated\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(script).to_json_string() + ", \"" + generated_path + "\")\n"
																			 "outputs=PackedStringArray(\"" +
				generated_path + "\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);

		ERR_PRINT_OFF;
		CHECK_EQ(FSLanguageProtocol::get_singleton()->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		CHECK(FileAccess::exists(ScopedLSPTempFile::resolve_path(generated_path)));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path(generated_path));
		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg"));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("Invalid pre-compile config blocks command execution before workspace indexing") {
		FSLanguageProtocol *proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String marker_path = "res://lsp/pre_compile_invalid_config_ran.txt";
		const String script =
				"import pathlib, sys\n"
				"path = pathlib.Path(sys.argv[1])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('ran\\n')\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(script).to_json_string() + ", \"" + marker_path + "\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);

		ERR_PRINT_OFF;
		CHECK_EQ(FSLanguageProtocol::get_singleton()->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		CHECK_FALSE(FileAccess::exists(ScopedLSPTempFile::resolve_path(marker_path)));

		Array notifications = TestFSLanguageProtocolInitializer::take_client_notifications(
				proto, "textDocument/publishDiagnostics");
		bool saw_validation_diagnostic = false;
		for (int i = 0; i < notifications.size(); i++) {
			Dictionary notification = notifications[i];
			Dictionary params = notification["params"];
			Array diagnostics = params["diagnostics"];
			for (int j = 0; j < diagnostics.size(); j++) {
				Dictionary diagnostic = diagnostics[j];
				if (String(diagnostic.get("message", "")).contains("outputs")) {
					saw_validation_diagnostic = true;
				}
			}
		}
		CHECK(saw_validation_diagnostic);

		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(proto);
		finish_language();
	}

	TEST_CASE("Changed pre-compile command inputs rerun before workspace indexing") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *first_proto = initialize(root);
		ProjectBuildTrustStore::set_cli_trusted_execution(true);

		const String input_path = "res://lsp/pre_compile_input.txt";
		const String generated_path = "res://lsp/pre_compile_input_generated.fs";
		const String script =
				"import pathlib, sys\n"
				"source = pathlib.Path(sys.argv[1]).read_text().strip()\n"
				"path = pathlib.Path(sys.argv[2])\n"
				"path.parent.mkdir(parents=True, exist_ok=True)\n"
				"path.write_text('class_name PreCompileInputGenerated\\nconst VALUE := \"%s\"\\n' % source)\n";
		const String config_text =
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate\")\n"
				"\n"
				"[build/tasks/generate]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"args=PackedStringArray(\"-c\", " +
				Variant(script).to_json_string() + ", \"" + input_path + "\", \"" + generated_path + "\")\n"
																									 "inputs=PackedStringArray(\"" +
				input_path + "\")\n"
							 "outputs=PackedStringArray(\"" +
				generated_path + "\")\n";
		ScopedLSPTempFile project_config("res://project.foundry", config_text);
		ScopedLSPTempFile input(input_path, "first");
		const String absolute_input_path = ScopedLSPTempFile::resolve_path(input_path);
		const String absolute_generated_path = ScopedLSPTempFile::resolve_path(generated_path);

		ERR_PRINT_OFF;
		CHECK_EQ(first_proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		Ref<FileAccess> generated = FileAccess::open(absolute_generated_path, FileAccess::READ);
		REQUIRE(generated.is_valid());
		CHECK(generated->get_as_utf8_string().contains("first"));
		memdelete(first_proto);
		finish_language();

		Ref<FileAccess> changed_input = FileAccess::open(absolute_input_path, FileAccess::WRITE);
		REQUIRE(changed_input.is_valid());
		changed_input->store_string("second");
		changed_input.unref();

		FSLanguageProtocol *second_proto = initialize(root);
		ERR_PRINT_OFF;
		CHECK_EQ(second_proto->get_workspace()->initialize(), OK);
		ERR_PRINT_ON;

		generated = FileAccess::open(absolute_generated_path, FileAccess::READ);
		REQUIRE(generated.is_valid());
		CHECK(generated->get_as_utf8_string().contains("second"));
		CHECK(FSLanguageProtocol::get_singleton()->peek_parse_result(generated_path) != nullptr);

		DirAccess::remove_absolute(absolute_generated_path);
		DirAccess::remove_absolute(ScopedLSPTempFile::resolve_path("res://.foundry/build_state.cfg"));
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		memdelete(second_proto);
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

		// The following tests are for all the link patterns specific to Foundry's built-in docs that we render as inline code.
		CHECK_EQ(LSP::marked_documentation("Class link: [Node2D], [Sprite2D]"), "Class link: `Node2D`, `Sprite2D`");
		CHECK_EQ(LSP::marked_documentation("Single class [RigidBody2D]"), "Single class `RigidBody2D`");
		CHECK_EQ(LSP::marked_documentation("[method Node2D.set_position]"), "`Node2D.set_position`");
		CHECK_EQ(LSP::marked_documentation("[member Node2D.position]"), "`Node2D.position`");
		CHECK_EQ(LSP::marked_documentation("[signal Node.ready]"), "`Node.ready`");
		CHECK_EQ(LSP::marked_documentation("[constant Color.RED]"), "`Color.RED`");
		CHECK_EQ(LSP::marked_documentation("[enum Node.ProcessMode]"), "`Node.ProcessMode`");
		CHECK_EQ(LSP::marked_documentation("[annotation @FoundryScript.@export]"), "`@FoundryScript.@export`");
		CHECK_EQ(LSP::marked_documentation("[constructor Vector2.Vector2]"), "`Vector2.Vector2`");
		CHECK_EQ(LSP::marked_documentation("[operator Vector2.operator +]"), "`Vector2.operator +`");
		CHECK_EQ(LSP::marked_documentation("[theme_item Button.font]"), "`Button.font`");
		CHECK_EQ(LSP::marked_documentation("[param delta]"), "`delta`");

		// Markdown links
		CHECK_EQ(LSP::marked_documentation("[url=https://docs.cafecito.games/foundry]link to Foundry[/url]"),
				"[link to Foundry](https://docs.cafecito.games/foundry)");
		CHECK_EQ(LSP::marked_documentation("[url]https://docs.cafecito.games/foundry/[/url]"),
				"[https://docs.cafecito.games/foundry/](https://docs.cafecito.games/foundry/)");

		// Code listings
		CHECK_EQ(LSP::marked_documentation("[codeblock]\nfunc test():\n    print(\"Hello, Foundry!\")\n[/codeblock]"),
				"```foundry_script\nfunc test():\n    print(\"Hello, Foundry!\")\n```");
		CHECK_EQ(LSP::marked_documentation("[codeblock lang=csharp]\npublic void Test()\n{\n    GD.Print(\"Hello, Foundry!\");\n}\n[/codeblock]"),
				"```csharp\npublic void Test()\n{\n    GD.Print(\"Hello, Foundry!\");\n}\n```");
		// Code listings with multiple languages (the codeblocks tag is used in the built-in reference)
		// When [codeblocks] is used, we only convert the [foundry_script] tag to a code block like the built-in editor.
		// NOTE: There is always a FoundryScript code listing in the built-in class reference.
		CHECK_EQ(LSP::marked_documentation("[codeblocks]\n[foundry_script]\nprint(hash(\"a\")) # Prints 177670\n[/foundry_script]\n[csharp]\nGD.Print(GD.Hash(\"a\")); // Prints 177670\n[/csharp]\n[/codeblocks]"),
				"```foundry_script\nprint(hash(\"a\")) # Prints 177670\n```\n");

		// lb and rb are used to insert literal square brackets in markdown.
		CHECK_EQ(LSP::marked_documentation("[lb]literal brackets[rb]"), "\\[literal brackets\\]");
		CHECK_EQ(LSP::marked_documentation("[lb]literal[rb] with [ClassName]"), "\\[literal\\] with `ClassName`");

		// We have to be careful that different patterns don't conflict with each
		// other, especially with urls that use brackets in markdown.
		CHECK_EQ(LSP::marked_documentation("Class [Sprite2D] with [url=https://docs.cafecito.games/foundry]link[/url]"),
				"Class `Sprite2D` with [link](https://docs.cafecito.games/foundry)");
	}

	struct DecodedSemanticToken {
		int line = 0;
		int start = 0;
		int length = 0;
		int type = 0;
		int modifiers = 0;
	};

	// Reverses the protocol's delta encoding so assertions can talk about absolute positions
	// instead of comparing opaque integer arrays, and checks the structural invariants of the
	// stream on the way through.
	Vector<DecodedSemanticToken> decode_semantic_tokens(const PackedInt32Array &p_data) {
		Vector<DecodedSemanticToken> decoded;
		REQUIRE_EQ(p_data.size() % 5, 0);

		int absolute_line = 0;
		int absolute_start = 0;
		for (int i = 0; i < p_data.size(); i += 5) {
			const int delta_line = p_data[i];
			const int delta_start = p_data[i + 1];
			const int length = p_data[i + 2];
			// Every field of a semantic token record is an unsigned integer on the wire, and a
			// zero-length token is not representable.
			CHECK(delta_line >= 0);
			CHECK(delta_start >= 0);
			CHECK(length > 0);

			absolute_line += delta_line;
			absolute_start = delta_line == 0 ? absolute_start + delta_start : delta_start;

			DecodedSemanticToken token;
			token.line = absolute_line;
			token.start = absolute_start;
			token.length = length;
			token.type = p_data[i + 3];
			token.modifiers = p_data[i + 4];
			decoded.push_back(token);
		}
		return decoded;
	}

	void check_semantic_token(const Vector<DecodedSemanticToken> &p_tokens, int p_index, int p_line, int p_start, int p_length, LSP::SemanticTokenType p_type, uint32_t p_modifiers = 0) {
		// `REQUIRE` does not unwind in this engine's `-fno-exceptions` builds, so guard the
		// indexing explicitly instead of letting a short result crash the whole run.
		REQUIRE(p_index < p_tokens.size());
		if (p_index >= p_tokens.size()) {
			return;
		}
		const DecodedSemanticToken &token = p_tokens[p_index];
		CHECK_EQ(token.line, p_line);
		CHECK_EQ(token.start, p_start);
		CHECK_EQ(token.length, p_length);
		CHECK_EQ(token.type, int(p_type));
		CHECK_EQ(uint32_t(token.modifiers), p_modifiers);
	}

	FSSemanticTokens::Span make_semantic_span(int p_line, int p_start_column, int p_length, LSP::SemanticTokenType p_type = LSP::SemanticTokenType::KEYWORD, uint32_t p_modifiers = 0) {
		FSSemanticTokens::Span span;
		span.line = p_line;
		span.start_column = p_start_column;
		span.length = p_length;
		span.type = p_type;
		span.modifiers = p_modifiers;
		return span;
	}

	Dictionary request_semantic_tokens(const String &p_uri) {
		Dictionary request;
		request["jsonrpc"] = "2.0";
		request["id"] = 7;
		request["method"] = "textDocument/semanticTokens/full";

		Dictionary params;
		params["textDocument"] = make_text_document_identifier(p_uri);
		request["params"] = params;
		return FSLanguageProtocol::get_singleton()->process_action(request);
	}

	PackedInt32Array semantic_token_data(const Dictionary &p_response) {
		REQUIRE(p_response.has("result"));
		Dictionary result = p_response["result"];
		REQUIRE(result.has("data"));
		return result["data"];
	}

	TEST_CASE("[textDocument][semanticTokens] advertises a stable legend and full-only support") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		TestFSLanguageProtocolInitializer::mark_initialized(proto);

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
		REQUIRE(capabilities["semanticTokensProvider"].get_type() == Variant::DICTIONARY);

		Dictionary provider = capabilities["semanticTokensProvider"];
		// `full` must be a plain boolean: a dictionary would advertise delta support.
		CHECK_EQ(provider["full"].get_type(), Variant::BOOL);
		CHECK(bool(provider["full"]));
		CHECK_EQ(provider["range"].get_type(), Variant::BOOL);
		CHECK_FALSE(bool(provider["range"]));

		Dictionary legend = provider["legend"];
		PackedStringArray token_types = legend["tokenTypes"];
		PackedStringArray expected_types;
		expected_types.push_back("namespace");
		expected_types.push_back("class");
		expected_types.push_back("interface");
		expected_types.push_back("struct");
		expected_types.push_back("enum");
		expected_types.push_back("enumMember");
		expected_types.push_back("event");
		expected_types.push_back("type");
		expected_types.push_back("typeParameter");
		expected_types.push_back("function");
		expected_types.push_back("method");
		expected_types.push_back("property");
		expected_types.push_back("variable");
		expected_types.push_back("parameter");
		expected_types.push_back("decorator");
		expected_types.push_back("keyword");
		CHECK_EQ(token_types, expected_types);

		PackedStringArray token_modifiers = legend["tokenModifiers"];
		PackedStringArray expected_modifiers;
		expected_modifiers.push_back("declaration");
		expected_modifiers.push_back("static");
		expected_modifiers.push_back("abstract");
		expected_modifiers.push_back("final");
		expected_modifiers.push_back("async");
		expected_modifiers.push_back("readonly");
		expected_modifiers.push_back("defaultLibrary");
		CHECK_EQ(token_modifiers, expected_modifiers);

		// The legend indices the encoder emits must line up with the advertised order.
		CHECK_EQ(token_types[int(LSP::SemanticTokenType::KEYWORD)], "keyword");
		CHECK_EQ(token_types[int(LSP::SemanticTokenType::NAMESPACE)], "namespace");
		CHECK_EQ(token_modifiers[int(LSP::SemanticTokenModifier::FINAL)], "final");
		CHECK_EQ(FSSemanticTokens::modifier_bit(LSP::SemanticTokenModifier::DECLARATION), 1u);
		CHECK_EQ(FSSemanticTokens::modifier_bit(LSP::SemanticTokenModifier::FINAL), 8u);

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[textDocument][semanticTokens] encodes UTF-16 delta records") {
		SUBCASE("an empty document produces no records") {
			CHECK_EQ(FSSemanticTokens::encode(Vector<FSSemanticTokens::Span>(), FSSemanticTokens::split_lines("")).size(), 0);
			CHECK_EQ(FSSemanticTokens::split_lines("").size(), 0);
		}

		SUBCASE("the first record is absolute and later records are relative") {
			Vector<String> lines = FSSemanticTokens::split_lines("var a = 1\nvar b = 2\nvar c = 3\n");
			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(1, 0, 3));
			spans.push_back(make_semantic_span(2, 0, 3));

			PackedInt32Array data = FSSemanticTokens::encode(spans, lines);
			REQUIRE_EQ(data.size(), 10);
			CHECK_EQ(data[0], 1); // Absolute line for the first record.
			CHECK_EQ(data[1], 0);
			CHECK_EQ(data[5], 1); // Relative line for the second record.

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(data);
			check_semantic_token(tokens, 0, 1, 0, 3, LSP::SemanticTokenType::KEYWORD);
			check_semantic_token(tokens, 1, 2, 0, 3, LSP::SemanticTokenType::KEYWORD);
		}

		SUBCASE("tokens on the same line encode a relative start") {
			Vector<String> lines = FSSemanticTokens::split_lines("if not ready:\n");
			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(0, 0, 2));
			spans.push_back(make_semantic_span(0, 3, 3));

			PackedInt32Array data = FSSemanticTokens::encode(spans, lines);
			REQUIRE_EQ(data.size(), 10);
			CHECK_EQ(data[5], 0); // Same line.
			CHECK_EQ(data[6], 3); // Relative to the previous start, not absolute.

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(data);
			check_semantic_token(tokens, 0, 0, 0, 2, LSP::SemanticTokenType::KEYWORD);
			check_semantic_token(tokens, 1, 0, 3, 3, LSP::SemanticTokenType::KEYWORD);
		}

		SUBCASE("a tab counts as a single code unit") {
			Vector<String> lines = FSSemanticTokens::split_lines("\t\tpass\n");
			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(0, 2, 4));

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(FSSemanticTokens::encode(spans, lines));
			REQUIRE_EQ(tokens.size(), 1);
			check_semantic_token(tokens, 0, 0, 2, 4, LSP::SemanticTokenType::KEYWORD);
		}

		SUBCASE("a token starting after an astral character is offset by a surrogate pair") {
			// `😀` is a single code point but two UTF-16 code units, so the token two code points
			// into the line starts at UTF-16 column 3.
			Vector<String> lines = FSSemanticTokens::split_lines(String::utf8("😀 var\n"));
			REQUIRE_EQ(lines[0].length(), 5);
			REQUIRE_EQ(FSSemanticTokens::utf16_length(lines[0]), 6);

			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(0, 2, 3, LSP::SemanticTokenType::VARIABLE));

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(FSSemanticTokens::encode(spans, lines));
			REQUIRE_EQ(tokens.size(), 1);
			check_semantic_token(tokens, 0, 0, 3, 3, LSP::SemanticTokenType::VARIABLE);
		}

		SUBCASE("a token containing an astral character is two code units longer") {
			Vector<String> lines = FSSemanticTokens::split_lines(String::utf8("var a = \"x😀y\"\n"));
			// The five code points of `"x😀y"` occupy six UTF-16 code units.
			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(0, 8, 5, LSP::SemanticTokenType::VARIABLE));

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(FSSemanticTokens::encode(spans, lines));
			REQUIRE_EQ(tokens.size(), 1);
			check_semantic_token(tokens, 0, 0, 8, 6, LSP::SemanticTokenType::VARIABLE);
		}

		SUBCASE("modifier bits round-trip through the encoding") {
			Vector<String> lines = FSSemanticTokens::split_lines("final var speed = 1\n");
			const uint32_t modifiers = FSSemanticTokens::modifier_bit(LSP::SemanticTokenModifier::DECLARATION) |
					FSSemanticTokens::modifier_bit(LSP::SemanticTokenModifier::FINAL) |
					FSSemanticTokens::modifier_bit(LSP::SemanticTokenModifier::READONLY);

			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(0, 10, 5, LSP::SemanticTokenType::PROPERTY, modifiers));

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(FSSemanticTokens::encode(spans, lines));
			REQUIRE_EQ(tokens.size(), 1);
			check_semantic_token(tokens, 0, 0, 10, 5, LSP::SemanticTokenType::PROPERTY, modifiers);
		}

		SUBCASE("unusable spans are dropped instead of corrupting the stream") {
			Vector<String> lines = FSSemanticTokens::split_lines("var a = 1\nvar b = 2\n");

			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(-1, 0, 3)); // Before the document.
			spans.push_back(make_semantic_span(9, 0, 3)); // After the document.
			spans.push_back(make_semantic_span(0, 0, 0)); // Empty.
			spans.push_back(make_semantic_span(0, -2, 3)); // Negative column.
			spans.push_back(make_semantic_span(0, 40, 3)); // Past the end of the line.
			spans.push_back(make_semantic_span(0, 0, 3, LSP::SemanticTokenType::MAX)); // Outside the legend.
			spans.push_back(make_semantic_span(0, 4, 1)); // The only valid span.
			spans.push_back(make_semantic_span(0, 4, 1)); // Overlaps the previous span.
			spans.push_back(make_semantic_span(0, 0, 3)); // Regresses behind the previous span.

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(FSSemanticTokens::encode(spans, lines));
			REQUIRE_EQ(tokens.size(), 1);
			check_semantic_token(tokens, 0, 0, 4, 1, LSP::SemanticTokenType::KEYWORD);
		}

		SUBCASE("a span that overruns its line is clamped to the line") {
			Vector<String> lines = FSSemanticTokens::split_lines("var a\n");
			Vector<FSSemanticTokens::Span> spans;
			spans.push_back(make_semantic_span(0, 4, 40));

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(FSSemanticTokens::encode(spans, lines));
			REQUIRE_EQ(tokens.size(), 1);
			check_semantic_token(tokens, 0, 0, 4, 1, LSP::SemanticTokenType::KEYWORD);
		}
	}

	TEST_CASE("[textDocument][semanticTokens] answers a full request through the protocol") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		Ref<FSTextDocument> text_document = proto->get_text_document();

		SUBCASE("reserved words are reported at their UTF-16 positions") {
			const String source =
					"var health = 1\n"
					"func heal():\n"
					"\tif not health:\n"
					"\t\tpass\n";
			const String uri = workspace->get_file_uri("res://lsp/semantic_tokens_basic.fs");
			text_document->didOpen(make_did_open_params(uri, source));

			PackedInt32Array data = semantic_token_data(request_semantic_tokens(uri));
			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(data);
			REQUIRE_EQ(tokens.size(), 5);
			check_semantic_token(tokens, 0, 0, 0, 3, LSP::SemanticTokenType::KEYWORD); // var
			check_semantic_token(tokens, 1, 1, 0, 4, LSP::SemanticTokenType::KEYWORD); // func
			check_semantic_token(tokens, 2, 2, 1, 2, LSP::SemanticTokenType::KEYWORD); // if
			check_semantic_token(tokens, 3, 2, 4, 3, LSP::SemanticTokenType::KEYWORD); // not
			check_semantic_token(tokens, 4, 3, 2, 4, LSP::SemanticTokenType::KEYWORD); // pass

			// The two tokens on line 2 must be encoded relative to each other.
			CHECK_EQ(data[15], 0);
			CHECK_EQ(data[16], 3);
		}

		SUBCASE("a reserved word in attribute position is not a keyword") {
			const String uri = workspace->get_file_uri("res://lsp/semantic_tokens_attribute.fs");
			text_document->didOpen(make_did_open_params(uri, "var kind = self.class\n"));

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(semantic_token_data(request_semantic_tokens(uri)));
			REQUIRE_EQ(tokens.size(), 2);
			check_semantic_token(tokens, 0, 0, 0, 3, LSP::SemanticTokenType::KEYWORD); // var
			check_semantic_token(tokens, 1, 0, 11, 4, LSP::SemanticTokenType::KEYWORD); // self
		}

		SUBCASE("a keyword after an astral character keeps UTF-16 columns") {
			const String uri = workspace->get_file_uri("res://lsp/semantic_tokens_astral.fs");
			text_document->didOpen(make_did_open_params(uri, String::utf8("# 😀 note\nvar a = 1\n")));

			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(semantic_token_data(request_semantic_tokens(uri)));
			REQUIRE_EQ(tokens.size(), 1);
			check_semantic_token(tokens, 0, 1, 0, 3, LSP::SemanticTokenType::KEYWORD);
		}

		SUBCASE("an empty document produces no records") {
			const String uri = workspace->get_file_uri("res://lsp/semantic_tokens_empty.fs");
			text_document->didOpen(make_did_open_params(uri, ""));

			CHECK_EQ(semantic_token_data(request_semantic_tokens(uri)).size(), 0);
		}

		SUBCASE("an unopened document that is not on disk produces no records") {
			const String uri = workspace->get_file_uri("res://lsp/semantic_tokens_missing.fs");

			CHECK_EQ(semantic_token_data(request_semantic_tokens(uri)).size(), 0);
		}

		SUBCASE("incomplete source still produces a well-formed partial result") {
			const String uri = workspace->get_file_uri("res://lsp/semantic_tokens_incomplete.fs");
			text_document->didOpen(make_did_open_params(uri, "func broken(:\n\tvar x = \"unterminated\n\t\t\tpass"));

			PackedInt32Array data = semantic_token_data(request_semantic_tokens(uri));
			Vector<DecodedSemanticToken> tokens = decode_semantic_tokens(data);
			REQUIRE_FALSE(tokens.is_empty());
			check_semantic_token(tokens, 0, 0, 0, 4, LSP::SemanticTokenType::KEYWORD); // func
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[textDocument][semanticTokens] reads the managed buffer, not disk") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		FSLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<FSWorkspace> workspace = FSLanguageProtocol::get_singleton()->get_workspace();
		Ref<FSTextDocument> text_document = proto->get_text_document();

		const String resource_path = "res://lsp/semantic_tokens_buffer.fs";
		ScopedLSPTempFile on_disk(resource_path, "var stale = 1\n");
		const String uri = workspace->get_file_uri(resource_path);

		// Before the client claims the document the server may only answer from disk.
		Vector<DecodedSemanticToken> disk_tokens = decode_semantic_tokens(semantic_token_data(request_semantic_tokens(uri)));
		REQUIRE_EQ(disk_tokens.size(), 1);
		check_semantic_token(disk_tokens, 0, 0, 0, 3, LSP::SemanticTokenType::KEYWORD); // var

		text_document->didOpen(make_did_open_params(uri, "func opened():\n\tpass\n"));
		Vector<DecodedSemanticToken> opened_tokens = decode_semantic_tokens(semantic_token_data(request_semantic_tokens(uri)));
		REQUIRE_EQ(opened_tokens.size(), 2);
		check_semantic_token(opened_tokens, 0, 0, 0, 4, LSP::SemanticTokenType::KEYWORD); // func
		check_semantic_token(opened_tokens, 1, 1, 1, 4, LSP::SemanticTokenType::KEYWORD); // pass

		text_document->didChange(make_did_change_params(uri, "class Changed:\n\tpass\n"));
		Vector<DecodedSemanticToken> changed_tokens = decode_semantic_tokens(semantic_token_data(request_semantic_tokens(uri)));
		REQUIRE_EQ(changed_tokens.size(), 2);
		check_semantic_token(changed_tokens, 0, 0, 0, 5, LSP::SemanticTokenType::KEYWORD); // class
		check_semantic_token(changed_tokens, 1, 1, 1, 4, LSP::SemanticTokenType::KEYWORD); // pass

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}
}

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
