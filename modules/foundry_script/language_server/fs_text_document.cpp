/**************************************************************************/
/*  fs_text_document.cpp                                                  */
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

#include "fs_text_document.h"

#include "../editor/fs_refactoring.h"
#include "../foundry_script.h"
#include "../fs_format.h"
#include "fs_extend_parser.h"
#include "fs_language_protocol.h"
#include "fs_semantic_tokens.h"

#include "core/io/file_access.h"
#include "editor/script/script_text_editor.h"
#include "editor/settings/editor_settings.h"
#include "servers/display/display_server.h"

namespace {

String refactor_kind_to_lsp_kind(RefactorKind p_kind) {
	switch (p_kind) {
		case RefactorKind::RENAME:
			return "refactor.rename";
		case RefactorKind::EXTRACT_VARIABLE:
		case RefactorKind::EXTRACT_METHOD:
			// LSP groups both extraction refactors under the same standard action kind.
			return "refactor.extract";
		case RefactorKind::ADD_TYPE_ANNOTATION:
		case RefactorKind::IMPLEMENT_ABSTRACT_METHODS:
		case RefactorKind::OVERRIDE_METHOD:
		case RefactorKind::INSERT_EXPLICIT_CAST:
		case RefactorKind::WIDEN_TO_NULLABLE:
		case RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE:
			return "refactor.rewrite";
		case RefactorKind::INLINE_VARIABLE:
			return "refactor.inline";
	}
	return "refactor";
}

bool code_action_kind_matches(const String &p_action_kind, const Array &p_only) {
	if (p_only.is_empty()) {
		return true;
	}
	for (int i = 0; i < p_only.size(); i++) {
		const String requested = p_only[i];
		if (p_action_kind == requested || p_action_kind.begins_with(requested + ".")) {
			return true;
		}
	}
	return false;
}

bool is_resolvable_code_action_kind(RefactorKind p_kind) {
	switch (p_kind) {
		case RefactorKind::EXTRACT_VARIABLE:
		case RefactorKind::EXTRACT_METHOD:
		case RefactorKind::ADD_TYPE_ANNOTATION:
		case RefactorKind::INLINE_VARIABLE:
		case RefactorKind::IMPLEMENT_ABSTRACT_METHODS:
		case RefactorKind::INSERT_EXPLICIT_CAST:
		case RefactorKind::WIDEN_TO_NULLABLE:
		case RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE:
			return true;
		case RefactorKind::OVERRIDE_METHOD:
		case RefactorKind::RENAME:
			return false;
	}
	return false;
}

void notify_refactor_message(int p_type, const String &p_message) {
	if (p_message.is_empty()) {
		return;
	}

	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	ERR_FAIL_NULL(protocol);

	LSP::ShowMessageParams params{
		p_type,
		p_message,
	};
	protocol->notify_client("window/showMessage", params.to_json());
}

Dictionary code_action_resolve_error(Dictionary &r_action, Dictionary &r_data, const String &p_message) {
	const String message = p_message.is_empty() ? "Refactor could not be resolved." : p_message;
	r_data["errorMessage"] = message;
	r_action["data"] = r_data;
	notify_refactor_message(LSP::MessageType::Error, "Cannot resolve code action: " + message);
	return r_action;
}

RefactorLocation refactor_location_from_lsp(const LSP::Range &p_range) {
	RefactorLocation loc;
	loc.start_line = p_range.start.line;
	loc.start_column = p_range.start.character;
	loc.end_line = p_range.end.line;
	loc.end_column = p_range.end.character;
	return loc;
}

String source_from_parser(const ExtendFSParser *p_parser) {
	return String("\n").join(p_parser->get_lines());
}

bool make_refactor_context(const String &p_uri, RefactorContext &r_context) {
	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	ERR_FAIL_NULL_V(protocol, false);

	Ref<FSWorkspace> workspace = protocol->get_workspace();
	ERR_FAIL_COND_V(workspace.is_null(), false);

	const String path = workspace->get_file_path(p_uri);
	if (path.is_empty()) {
		return false;
	}

	r_context.path = path;

	if (const ExtendFSParser *parser = protocol->get_parse_result(path)) {
		r_context.source = source_from_parser(parser);
		return true;
	}

	Error err = OK;
	r_context.source = FileAccess::get_file_as_string(path, &err);
	return err == OK;
}

LSP::TextEdit lsp_text_edit_from_refactor(const RefactorTextEdit &p_edit) {
	LSP::TextEdit edit;
	edit.range.start.line = p_edit.start_line;
	edit.range.start.character = p_edit.start_column;
	edit.range.end.line = p_edit.end_line;
	edit.range.end.character = p_edit.end_column;
	edit.newText = p_edit.new_text;
	return edit;
}

bool add_refactor_edits_to_workspace_edit(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const Vector<RefactorTextEdit> &p_edits,
		LSP::WorkspaceEdit &r_workspace_edit) {
	if (p_path.is_empty()) {
		return false;
	}
	const String uri = p_workspace->get_file_uri(p_path);
	if (uri.is_empty()) {
		return false;
	}
	for (const RefactorTextEdit &edit : p_edits) {
		r_workspace_edit.add_edit(uri, lsp_text_edit_from_refactor(edit));
	}
	return true;
}

bool workspace_edit_from_refactor_result(
		const RefactorResult &p_result,
		const RefactorContext &p_context,
		LSP::WorkspaceEdit &r_workspace_edit) {
	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	ERR_FAIL_NULL_V(protocol, false);

	Ref<FSWorkspace> workspace = protocol->get_workspace();
	ERR_FAIL_COND_V(workspace.is_null(), false);

	if (!p_result.file_edits.is_empty()) {
		for (const RefactorFileEdit &file_edit : p_result.file_edits) {
			if (!add_refactor_edits_to_workspace_edit(workspace, file_edit.path, file_edit.edits, r_workspace_edit)) {
				return false;
			}
		}
		return true;
	}

	return add_refactor_edits_to_workspace_edit(workspace, p_context.path, p_result.edits, r_workspace_edit);
}

Dictionary refactor_action_data(const String &p_uri, const LSP::Range &p_range, RefactorKind p_kind) {
	Dictionary data;
	data["godotRefactor"] = true;
	data["uri"] = p_uri;
	data["range"] = p_range.to_json();
	data["kind"] = (int)p_kind;
	return data;
}

Dictionary code_action_for_availability(
		const RefactorAvailability &p_availability,
		const String &p_uri,
		const LSP::Range &p_range) {
	LSP::CodeAction action;
	action.title = p_availability.title;
	action.kind = refactor_kind_to_lsp_kind(p_availability.kind);
	action.data = refactor_action_data(p_uri, p_range, p_availability.kind);
	return action.to_json();
}

} // namespace

void FSTextDocument::_bind_methods() {
	ClassDB::bind_method(D_METHOD("didOpen"), &FSTextDocument::didOpen);
	ClassDB::bind_method(D_METHOD("didClose"), &FSTextDocument::didClose);
	ClassDB::bind_method(D_METHOD("didChange"), &FSTextDocument::didChange);
	ClassDB::bind_method(D_METHOD("willSaveWaitUntil"), &FSTextDocument::willSaveWaitUntil);
	ClassDB::bind_method(D_METHOD("didSave"), &FSTextDocument::didSave);
	ClassDB::bind_method(D_METHOD("nativeSymbol"), &FSTextDocument::nativeSymbol);
	ClassDB::bind_method(D_METHOD("documentSymbol"), &FSTextDocument::documentSymbol);
	ClassDB::bind_method(D_METHOD("completion"), &FSTextDocument::completion);
	ClassDB::bind_method(D_METHOD("resolve"), &FSTextDocument::resolve);
	ClassDB::bind_method(D_METHOD("rename"), &FSTextDocument::rename);
	ClassDB::bind_method(D_METHOD("prepareRename"), &FSTextDocument::prepareRename);
	ClassDB::bind_method(D_METHOD("codeAction"), &FSTextDocument::codeAction);
	ClassDB::bind_method(D_METHOD("resolveCodeAction"), &FSTextDocument::resolveCodeAction);
	ClassDB::bind_method(D_METHOD("references"), &FSTextDocument::references);
	ClassDB::bind_method(D_METHOD("foldingRange"), &FSTextDocument::foldingRange);
	ClassDB::bind_method(D_METHOD("codeLens"), &FSTextDocument::codeLens);
	ClassDB::bind_method(D_METHOD("documentLink"), &FSTextDocument::documentLink);
	ClassDB::bind_method(D_METHOD("colorPresentation"), &FSTextDocument::colorPresentation);
	ClassDB::bind_method(D_METHOD("hover"), &FSTextDocument::hover);
	ClassDB::bind_method(D_METHOD("definition"), &FSTextDocument::definition);
	ClassDB::bind_method(D_METHOD("declaration"), &FSTextDocument::declaration);
	ClassDB::bind_method(D_METHOD("signatureHelp"), &FSTextDocument::signatureHelp);
	ClassDB::bind_method(D_METHOD("semanticTokensFull"), &FSTextDocument::semanticTokensFull);
	ClassDB::bind_method(D_METHOD("show_native_symbol_in_editor"), &FSTextDocument::show_native_symbol_in_editor);
}

void FSTextDocument::didOpen(const Variant &p_param) {
	FSLanguageProtocol::get_singleton()->lsp_did_open(p_param);
}

void FSTextDocument::didChange(const Variant &p_param) {
	FSLanguageProtocol::get_singleton()->lsp_did_change(p_param);
}

void FSTextDocument::didClose(const Variant &p_param) {
	FSLanguageProtocol::get_singleton()->lsp_did_close(p_param);
}

void FSTextDocument::willSaveWaitUntil(const Variant &p_param) {
	Dictionary dict = p_param;
	LSP::TextDocumentIdentifier doc;
	doc.load(dict["textDocument"]);

	String path = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_path(doc.uri);
	Ref<Script> scr = ResourceLoader::load(path);
	if (scr.is_valid()) {
		ScriptEditor::get_singleton()->clear_docs_from_script(scr);
	}
}

void FSTextDocument::didSave(const Variant &p_param) {
	Dictionary dict = p_param;
	LSP::TextDocumentIdentifier doc;
	doc.load(dict["textDocument"]);
	String text = dict["text"];

	String path = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_path(doc.uri);
	Ref<FoundryScript> scr = ResourceLoader::load(path);
	if (scr.is_valid() && (scr->load_source_code(path) == OK)) {
		if (scr->is_tool()) {
			scr->get_language()->reload_tool_script(scr, true);
		} else {
			scr->reload(true);
		}

		scr->update_exports();

		if (!Thread::is_main_thread()) {
			callable_mp(this, &FSTextDocument::reload_script).call_deferred(scr);
		} else {
			reload_script(scr);
		}
	}
}

void FSTextDocument::reload_script(Ref<FoundryScript> p_to_reload_script) {
	ScriptEditor::get_singleton()->reload_scripts(true);
	ScriptEditor::get_singleton()->update_docs_from_script(p_to_reload_script);
	ScriptEditor::get_singleton()->trigger_live_script_reload(p_to_reload_script->get_path());
}

void FSTextDocument::notify_client_show_symbol(const LSP::DocumentSymbol *symbol) {
	ERR_FAIL_NULL(symbol);
	FSLanguageProtocol::get_singleton()->notify_client("foundry_script/show_native_symbol", symbol->to_json(true));
}

void FSTextDocument::initialize() {
	if (FSLanguageProtocol::get_singleton()->is_smart_resolve_enabled()) {
		for (const KeyValue<StringName, ClassMembers> &E : FSLanguageProtocol::get_singleton()->get_workspace()->native_members) {
			const ClassMembers &members = E.value;

			for (const KeyValue<String, const LSP::DocumentSymbol *> &F : members) {
				const LSP::DocumentSymbol *symbol = members.get(F.key);
				LSP::CompletionItem item = symbol->make_completion_item();
				item.data = JOIN_SYMBOLS(String(E.key), F.key);
				native_member_completions.push_back(item.to_json());
			}
		}
	}
}

Variant FSTextDocument::nativeSymbol(const Dictionary &p_params) {
	Variant ret;

	LSP::NativeSymbolInspectParams params;
	params.load(p_params);

	if (const LSP::DocumentSymbol *symbol = FSLanguageProtocol::get_singleton()->get_workspace()->resolve_native_symbol(params)) {
		ret = symbol->to_json(true);
		notify_client_show_symbol(symbol);
	}

	return ret;
}

Array FSTextDocument::documentSymbol(const Dictionary &p_params) {
	Dictionary params = p_params["textDocument"];
	String uri = params["uri"];
	String path = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_path(uri);
	Array arr;

	ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(path);
	if (parser) {
		LSP::DocumentSymbol symbol = parser->get_symbols();
		arr.push_back(symbol.to_json(true));
	}
	return arr;
}

Array FSTextDocument::completion(const Dictionary &p_params) {
	Array arr;

	LSP::CompletionParams params;
	params.load(p_params);
	Dictionary request_data = params.to_json();

	List<ScriptLanguage::CodeCompletionOption> options;
	FSLanguageProtocol::get_singleton()->get_workspace()->completion(params, &options);

	if (!options.is_empty()) {
		int i = 0;
		arr.resize(options.size());

		for (const ScriptLanguage::CodeCompletionOption &option : options) {
			LSP::CompletionItem item;
			item.label = option.display;
			item.data = request_data;
			item.insertText = option.insert_text;

			switch (option.kind) {
				case ScriptLanguage::CODE_COMPLETION_KIND_ENUM:
					item.kind = LSP::CompletionItemKind::Enum;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_CLASS:
					item.kind = LSP::CompletionItemKind::Class;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_MEMBER:
					item.kind = LSP::CompletionItemKind::Property;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_FUNCTION:
					item.kind = LSP::CompletionItemKind::Method;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_SIGNAL:
					item.kind = LSP::CompletionItemKind::Event;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_CONSTANT:
					item.kind = LSP::CompletionItemKind::Constant;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_VARIABLE:
					item.kind = LSP::CompletionItemKind::Variable;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_FILE_PATH:
					item.kind = LSP::CompletionItemKind::File;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_NODE_PATH:
					item.kind = LSP::CompletionItemKind::Snippet;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT:
					item.kind = LSP::CompletionItemKind::Text;
					break;
				default: {
				}
			}

			arr[i] = item.to_json();
			i++;
		}
	}
	return arr;
}

Dictionary FSTextDocument::rename(const Dictionary &p_params) {
	LSP::TextDocumentPositionParams params;
	params.load(p_params);
	String new_name = p_params["newName"];

	RefactorContext ctx;
	if (!make_refactor_context(params.textDocument.uri, ctx)) {
		return LSP::WorkspaceEdit().to_json();
	}

	RefactorParams refactor_params;
	refactor_params.new_name = new_name;

	LSP::Range range;
	range.start = params.position;
	range.end = params.position;
	const RefactorLocation loc = refactor_location_from_lsp(range);
	RefactorResult result = FSRefactoring::prepare(ctx, loc, RefactorKind::RENAME, refactor_params);
	if (!result.ok) {
		return LSP::WorkspaceEdit().to_json();
	}

	LSP::WorkspaceEdit edit;
	if (!workspace_edit_from_refactor_result(result, ctx, edit)) {
		return LSP::WorkspaceEdit().to_json();
	}
	notify_refactor_message(LSP::MessageType::Warning, result.warning);
	return edit.to_json();
}

Variant FSTextDocument::prepareRename(const Dictionary &p_params) {
	LSP::TextDocumentPositionParams params;
	params.load(p_params);

	LSP::DocumentSymbol symbol;
	LSP::Range range;
	if (FSLanguageProtocol::get_singleton()->get_workspace()->can_rename(params, symbol, range)) {
		return Variant(range.to_json());
	}

	// `null` -> rename not valid at current location.
	return Variant();
}

Array FSTextDocument::codeAction(const Dictionary &p_params) {
	Array actions;

	LSP::CodeActionParams params;
	params.load(p_params);

	RefactorContext ctx;
	if (!make_refactor_context(params.textDocument.uri, ctx)) {
		return actions;
	}

	const RefactorLocation loc = refactor_location_from_lsp(params.range);
	const Vector<RefactorAvailability> available = FSRefactoring::get_available_refactors(ctx, loc);
	for (const RefactorAvailability &availability : available) {
		if (!availability.enabled || availability.kind == RefactorKind::RENAME ||
				availability.kind == RefactorKind::OVERRIDE_METHOD) {
			continue;
		}

		const String action_kind = refactor_kind_to_lsp_kind(availability.kind);
		if (!code_action_kind_matches(action_kind, params.context.only)) {
			continue;
		}

		Dictionary action = code_action_for_availability(availability, params.textDocument.uri, params.range);
		if (!action.is_empty()) {
			actions.push_back(action);
		}
	}

	return actions;
}

Dictionary FSTextDocument::resolveCodeAction(const Dictionary &p_params) {
	Dictionary action = p_params;
	Variant data_variant = action.get("data", Variant());
	if (data_variant.get_type() != Variant::DICTIONARY) {
		return action;
	}

	Dictionary data = data_variant;
	if (!bool(data.get("godotRefactor", false))) {
		return action;
	}

	const RefactorKind kind = (RefactorKind)(int)data.get("kind", -1);
	if (!is_resolvable_code_action_kind(kind)) {
		return action;
	}

	const String uri = data.get("uri", "");
	if (uri.is_empty() || !data.has("range")) {
		return code_action_resolve_error(action, data, "Cannot resolve refactor action data.");
	}

	LSP::Range range;
	range.load(data["range"]);

	RefactorContext ctx;
	if (!make_refactor_context(uri, ctx)) {
		return code_action_resolve_error(action, data, "Cannot read refactor source.");
	}

	RefactorParams refactor_params;
	RefactorResult result = FSRefactoring::prepare(ctx, refactor_location_from_lsp(range), kind, refactor_params);
	if (!result.ok) {
		return code_action_resolve_error(action, data, result.error_message);
	}

	LSP::WorkspaceEdit edit;
	if (!workspace_edit_from_refactor_result(result, ctx, edit)) {
		return code_action_resolve_error(action, data, "Cannot convert refactor edits.");
	}

	action["edit"] = edit.to_json();
	notify_refactor_message(LSP::MessageType::Warning, result.warning);
	return action;
}

Array FSTextDocument::references(const Dictionary &p_params) {
	Array res;

	LSP::ReferenceParams params;
	params.load(p_params);

	const LSP::DocumentSymbol *symbol = FSLanguageProtocol::get_singleton()->get_workspace()->resolve_symbol(params);
	if (symbol) {
		Vector<LSP::Location> usages = FSLanguageProtocol::get_singleton()->get_workspace()->find_all_usages(*symbol);
		res.resize(usages.size());
		int declaration_adjustment = 0;
		for (int i = 0; i < usages.size(); i++) {
			LSP::Location usage = usages[i];
			if (!params.context.includeDeclaration && usage.range == symbol->range) {
				declaration_adjustment++;
				continue;
			}
			res[i - declaration_adjustment] = usages[i].to_json();
		}

		if (declaration_adjustment > 0) {
			res.resize(res.size() - declaration_adjustment);
		}
	}

	return res;
}

Dictionary FSTextDocument::resolve(const Dictionary &p_params) {
	LSP::CompletionItem item;
	item.load(p_params);

	LSP::CompletionParams params;
	Variant data = p_params["data"];

	const LSP::DocumentSymbol *symbol = nullptr;

	if (data.get_type() == Variant::DICTIONARY) {
		params.load(p_params["data"]);
		symbol = FSLanguageProtocol::get_singleton()->get_workspace()->resolve_symbol(params, item.label, item.kind == LSP::CompletionItemKind::Method || item.kind == LSP::CompletionItemKind::Function);

	} else if (data.is_string()) {
		String query = data;

		Vector<String> param_symbols = query.split(SYMBOL_SEPARATOR, false);

		if (param_symbols.size() >= 2) {
			StringName class_name = param_symbols[0];
			const String &member_name = param_symbols[param_symbols.size() - 1];
			String inner_class_name;
			if (param_symbols.size() >= 3) {
				inner_class_name = param_symbols[1];
			}

			if (const ClassMembers *members = FSLanguageProtocol::get_singleton()->get_workspace()->native_members.getptr(class_name)) {
				if (const LSP::DocumentSymbol *const *member = members->getptr(member_name)) {
					symbol = *member;
				}
			}

			if (!symbol) {
				ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(class_name);
				if (parser) {
					symbol = parser->get_member_symbol(member_name, inner_class_name);
				}
			}
		}
	}

	if (symbol) {
		item.detail = symbol->detail;
		item.documentation = symbol->render();
	}

	if (item.kind == LSP::CompletionItemKind::Event) {
		if (params.context.triggerKind == LSP::CompletionTriggerKind::TriggerCharacter && (params.context.triggerCharacter == "(")) {
			const String quote_style = EDITOR_GET("text_editor/completion/use_single_quotes") ? "'" : "\"";
			item.insertText = item.label.quote(quote_style);
		}
	}

	if (item.kind == LSP::CompletionItemKind::Method) {
		bool is_trigger_character = params.context.triggerKind == LSP::CompletionTriggerKind::TriggerCharacter;
		bool is_quote_character = params.context.triggerCharacter == "\"" || params.context.triggerCharacter == "'";

		if (is_trigger_character && is_quote_character && item.insertText.is_quoted()) {
			item.insertText = item.insertText.unquote();
		}
	}

	return item.to_json(true);
}

Array FSTextDocument::foldingRange(const Dictionary &p_params) {
	return Array();
}

Array FSTextDocument::codeLens(const Dictionary &p_params) {
	return Array();
}

Array FSTextDocument::documentLink(const Dictionary &p_params) {
	Array ret;

	LSP::DocumentLinkParams params;
	params.load(p_params);

	List<LSP::DocumentLink> links;
	FSLanguageProtocol::get_singleton()->get_workspace()->resolve_document_links(params.textDocument.uri, links);
	for (const LSP::DocumentLink &E : links) {
		ret.push_back(E.to_json());
	}
	return ret;
}

Array FSTextDocument::colorPresentation(const Dictionary &p_params) {
	return Array();
}

Variant FSTextDocument::hover(const Dictionary &p_params) {
	LSP::TextDocumentPositionParams params;
	params.load(p_params);

	const LSP::DocumentSymbol *symbol = FSLanguageProtocol::get_singleton()->get_workspace()->resolve_symbol(params);
	if (symbol) {
		LSP::Hover hover;
		hover.contents = symbol->render();
		hover.range.start = params.position;
		hover.range.end = params.position;
		return hover.to_json();

	} else if (FSLanguageProtocol::get_singleton()->is_smart_resolve_enabled()) {
		Dictionary ret;
		Array contents;
		List<const LSP::DocumentSymbol *> list;
		FSLanguageProtocol::get_singleton()->resolve_related_symbols(params, list);
		for (const LSP::DocumentSymbol *&E : list) {
			if (const LSP::DocumentSymbol *s = E) {
				contents.push_back(s->render().value);
			}
		}
		ret["contents"] = contents;
		return ret;
	}

	return Variant();
}

Array FSTextDocument::definition(const Dictionary &p_params) {
	LSP::TextDocumentPositionParams params;
	params.load(p_params);
	List<const LSP::DocumentSymbol *> symbols;
	return find_symbols(params, symbols);
}

Variant FSTextDocument::declaration(const Dictionary &p_params) {
	LSP::TextDocumentPositionParams params;
	params.load(p_params);
	List<const LSP::DocumentSymbol *> symbols;
	Array arr = find_symbols(params, symbols);
	if (arr.is_empty() && !symbols.is_empty() && !symbols.front()->get()->native_class.is_empty()) { // Find a native symbol
		const LSP::DocumentSymbol *symbol = symbols.front()->get();
		if (FSLanguageProtocol::get_singleton()->is_goto_native_symbols_enabled()) {
			String id;
			switch (symbol->kind) {
				case LSP::SymbolKind::Class:
					id = "class_name:" + symbol->name;
					break;
				case LSP::SymbolKind::Constant:
					id = "class_constant:" + symbol->native_class + ":" + symbol->name;
					break;
				case LSP::SymbolKind::Property:
				case LSP::SymbolKind::Variable:
					id = "class_property:" + symbol->native_class + ":" + symbol->name;
					break;
				case LSP::SymbolKind::Enum:
					id = "class_enum:" + symbol->native_class + ":" + symbol->name;
					break;
				case LSP::SymbolKind::Method:
				case LSP::SymbolKind::Function:
					id = "class_method:" + symbol->native_class + ":" + symbol->name;
					break;
				default:
					id = "class_global:" + symbol->native_class + ":" + symbol->name;
					break;
			}
			callable_mp(this, &FSTextDocument::show_native_symbol_in_editor).call_deferred(id);
		} else {
			notify_client_show_symbol(symbol);
		}
	}
	return arr;
}

Variant FSTextDocument::signatureHelp(const Dictionary &p_params) {
	Variant ret;

	LSP::TextDocumentPositionParams params;
	params.load(p_params);

	LSP::SignatureHelp s;
	if (OK == FSLanguageProtocol::get_singleton()->get_workspace()->resolve_signature(params, s)) {
		ret = s.to_json();
	}

	return ret;
}

Array FSTextDocument::formatting(const Dictionary &p_params) {
	Array edits;

	LSP::DocumentFormattingParams params;
	params.load(p_params);

	// `make_refactor_context` reconstructs the exact in-editor buffer (it joins the
	// parser's lines, the inverse of the `split("\n")` that produced them), so the
	// replace range computed below lines up with what the client holds.
	RefactorContext context;
	if (!make_refactor_context(params.textDocument.uri, context)) {
		return edits;
	}

	String formatted;
	String error_message;
	if (!FSLanguage::get_singleton()->format_code(context.source, context.path, formatted, &error_message)) {
		// The core refuses on parse error; return no edits and surface the diagnostic.
		if (!error_message.is_empty()) {
			notify_refactor_message(LSP::MessageType::Warning, "Cannot format document: " + error_message);
		}
		return edits;
	}

	if (formatted == context.source) {
		// Already canonical: no edit avoids a redundant client-side change.
		return edits;
	}

	// A single whole-document replacement. The end position is the spot just past
	// the buffer's final character, walked so a trailing newline lands on its own
	// (empty) line rather than overshooting. LSP character offsets are counted in
	// UTF-16 code units, so a non-BMP code point (e.g. an emoji in a string literal)
	// advances the column by two.
	LSP::TextEdit edit;
	edit.range.start.line = 0;
	edit.range.start.character = 0;
	int end_line = 0;
	int end_character = 0;
	for (int i = 0; i < context.source.length(); i++) {
		const char32_t character = context.source[i];
		if (character == '\n') {
			end_line++;
			end_character = 0;
		} else {
			end_character += (character > 0xFFFF) ? 2 : 1;
		}
	}
	edit.range.end.line = end_line;
	edit.range.end.character = end_character;
	edit.newText = formatted;

	edits.push_back(edit.to_json());
	return edits;
}

Dictionary FSTextDocument::semanticTokensFull(const Dictionary &p_params) {
	LSP::SemanticTokens tokens;

	LSP::SemanticTokensParams params;
	params.load(p_params);

	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	ERR_FAIL_NULL_V(protocol, tokens.to_json());

	Ref<FSWorkspace> workspace = protocol->get_workspace();
	ERR_FAIL_COND_V(workspace.is_null(), tokens.to_json());

	const String path = workspace->get_file_path(params.textDocument.uri);
	if (path.is_empty()) {
		return tokens.to_json();
	}

	// The managed buffer is authoritative: a client that has sent `didOpen`/`didChange` expects its
	// unsaved text to be highlighted, not whatever is still on disk.
	String source;
	if (!protocol->get_managed_document_text(path, source)) {
		if (!path.has_extension("fs")) {
			return tokens.to_json();
		}
		Error err = OK;
		source = FileAccess::get_file_as_string(path, &err);
		if (err != OK) {
			return tokens.to_json();
		}
	}

	const Vector<String> lines = FSSemanticTokens::split_lines(source);
	tokens.data = FSSemanticTokens::encode(FSSemanticTokens::collect(source, lines), lines);
	return tokens.to_json();
}

FSTextDocument::FSTextDocument() {
	file_checker = FileAccess::create(FileAccess::ACCESS_RESOURCES);
}

void FSTextDocument::show_native_symbol_in_editor(const String &p_symbol_id) {
	callable_mp(ScriptEditor::get_singleton(), &ScriptEditor::goto_help).call_deferred(p_symbol_id);

	DisplayServer::get_singleton()->window_move_to_foreground();
}

Array FSTextDocument::find_symbols(const LSP::TextDocumentPositionParams &p_location, List<const LSP::DocumentSymbol *> &r_list) {
	Array arr;
	const LSP::DocumentSymbol *symbol = FSLanguageProtocol::get_singleton()->get_workspace()->resolve_symbol(p_location);
	if (symbol) {
		LSP::Location location;
		location.uri = symbol->uri;
		if (!location.uri.is_empty()) {
			location.range = symbol->selectionRange;
			const String &path = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_path(symbol->uri);
			if (file_checker->file_exists(path)) {
				arr.push_back(location.to_json());
			}
		}
		r_list.push_back(symbol);
	} else if (FSLanguageProtocol::get_singleton()->is_smart_resolve_enabled()) {
		List<const LSP::DocumentSymbol *> list;
		FSLanguageProtocol::get_singleton()->resolve_related_symbols(p_location, list);
		for (const LSP::DocumentSymbol *&E : list) {
			if (const LSP::DocumentSymbol *s = E) {
				if (!s->uri.is_empty()) {
					LSP::Location location;
					location.uri = s->uri;
					location.range = s->selectionRange;
					arr.push_back(location.to_json());
					r_list.push_back(s);
				}
			}
		}
	}
	return arr;
}
