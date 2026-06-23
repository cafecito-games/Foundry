/**************************************************************************/
/*  extract_method_name_prompt.cpp                                        */
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

#include "extract_method_name_prompt.h"

#ifdef TOOLS_ENABLED

void ExtractMethodNamePromptModel::_validate() {
	valid = false;
	error_message = String();

	if (!pending) {
		return;
	}

	if (name.is_empty()) {
		error_message = "Name cannot be empty.";
		return;
	}

	RefactorParams params;
	params.new_name = name;
	const RefactorResult result = GDScriptRefactoring::prepare(context, location, RefactorKind::EXTRACT_METHOD, params);
	if (!result.ok) {
		error_message = result.error_message;
		return;
	}

	valid = true;
}

void ExtractMethodNamePromptModel::begin(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const String &p_suggested_name) {
	pending = true;
	context = p_context;
	location = p_location;
	name = p_suggested_name;
	_validate();
}

void ExtractMethodNamePromptModel::set_name(const String &p_name) {
	name = p_name;
	_validate();
}

void ExtractMethodNamePromptModel::cancel() {
	clear();
}

void ExtractMethodNamePromptModel::clear() {
	pending = false;
	valid = false;
	context = RefactorContext();
	location = RefactorLocation();
	name = String();
	error_message = String();
}

bool ExtractMethodNamePromptModel::has_pending_request() const {
	return pending;
}

bool ExtractMethodNamePromptModel::is_valid() const {
	return valid;
}

String ExtractMethodNamePromptModel::get_name() const {
	return name;
}

String ExtractMethodNamePromptModel::get_error_message() const {
	return error_message;
}

const RefactorLocation &ExtractMethodNamePromptModel::get_location() const {
	return location;
}

bool ExtractMethodNamePromptModel::confirm(String &r_name) const {
	if (!pending || !valid) {
		return false;
	}
	r_name = name;
	return true;
}

#endif // TOOLS_ENABLED
