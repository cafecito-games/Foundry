/**************************************************************************/
/*  gdscript_refactoring.cpp                                              */
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

#include "gdscript_refactoring.h"

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring_names.h"

#ifndef GDSCRIPT_NO_LSP
#include "../language_server/gdscript_language_protocol.h"
#include "../language_server/gdscript_workspace.h"
#include "../language_server/godot_lsp.h"

static LSP::TextDocumentPositionParams make_document_position(const Ref<GDScriptWorkspace> &p_workspace, const RefactorContext &p_context, const RefactorLocation &p_location) {
	LSP::TextDocumentPositionParams doc_position;
	doc_position.textDocument.uri = p_workspace->get_file_uri(p_context.path);
	doc_position.position.line = p_location.start_line;
	doc_position.position.character = p_location.start_column;
	return doc_position;
}
#endif // GDSCRIPT_NO_LSP

static RefactorResult prepare_rename(const RefactorContext &p_context, const RefactorLocation &p_location, const RefactorParams &p_params) {
	RefactorResult result;

#ifdef GDSCRIPT_NO_LSP
	result.ok = false;
	result.error_message = "Rename requires the language server, which is not available in this build.";
	return result;
#else
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	Ref<GDScriptWorkspace> workspace = protocol ? protocol->get_workspace() : Ref<GDScriptWorkspace>();
	if (workspace.is_null()) {
		result.ok = false;
		result.error_message = "Rename requires the language server, which is not available in this build.";
		return result;
	}

	LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);

	LSP::DocumentSymbol symbol;
	LSP::Range identifier_range;
	if (!workspace->can_rename(doc_position, symbol, identifier_range)) {
		result.ok = false;
		result.error_message = "Cannot rename this symbol.";
		return result;
	}

	String reason;
	if (!GDScriptRefactorNames::validate_identifier(p_params.new_name, reason)) {
		result.ok = false;
		result.error_message = reason;
		return result;
	}

	// `find_all_usages` matches usages by the address of the resolved symbol, so it must
	// receive the workspace-owned symbol rather than the copy returned by `can_rename`.
	const LSP::DocumentSymbol *resolved_symbol = workspace->resolve_symbol(doc_position);
	if (!resolved_symbol) {
		result.ok = false;
		result.error_message = "Cannot rename this symbol.";
		return result;
	}

	const Vector<LSP::Location> usages = workspace->find_all_usages(*resolved_symbol);
	for (const LSP::Location &usage : usages) {
		if (usage.uri != doc_position.textDocument.uri) {
			continue;
		}
		RefactorTextEdit edit;
		edit.start_line = usage.range.start.line;
		edit.start_column = usage.range.start.character;
		edit.end_line = usage.range.end.line;
		edit.end_column = usage.range.end.character;
		edit.new_text = p_params.new_name;
		result.edits.push_back(edit);
	}

	if (result.edits.is_empty()) {
		result.ok = false;
		result.error_message = "No references found to rename.";
		return result;
	}

	result.ok = true;
	result.suggested_name = symbol.name;
	result.rename_anchor_line = identifier_range.start.line;
	result.rename_anchor_column = identifier_range.start.character;
	return result;
#endif // GDSCRIPT_NO_LSP
}

Vector<RefactorAvailability> GDScriptRefactoring::get_available_refactors(const RefactorContext &p_context, const RefactorLocation &p_location) {
	Vector<RefactorAvailability> result;

	RefactorAvailability rename;
	rename.kind = RefactorKind::RENAME;
	rename.title = "Rename Symbol";

#ifdef GDSCRIPT_NO_LSP
	rename.enabled = false;
	rename.disabled_reason = "Rename requires the language server.";
#else
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	Ref<GDScriptWorkspace> workspace = protocol ? protocol->get_workspace() : Ref<GDScriptWorkspace>();
	if (workspace.is_null()) {
		rename.enabled = false;
		rename.disabled_reason = "Rename requires the language server.";
	} else {
		LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);
		LSP::DocumentSymbol symbol;
		LSP::Range identifier_range;
		rename.enabled = workspace->can_rename(doc_position, symbol, identifier_range);
		if (!rename.enabled) {
			rename.disabled_reason = "Place the caret on a renameable symbol.";
		}
	}
#endif // GDSCRIPT_NO_LSP

	result.push_back(rename);
	return result;
}

RefactorResult GDScriptRefactoring::prepare(const RefactorContext &p_context, const RefactorLocation &p_location, RefactorKind p_kind, const RefactorParams &p_params) {
	switch (p_kind) {
		case RefactorKind::RENAME:
			return prepare_rename(p_context, p_location, p_params);
		default:
			break;
	}

	RefactorResult result;
	result.ok = false;
	result.error_message = "Refactor not implemented.";
	return result;
}

#endif // TOOLS_ENABLED
