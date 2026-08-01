/**************************************************************************/
/*  fs_builtin_types.cpp                                                  */
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

#include "fs_builtin_types.h"

#include "builtin/fs_builtin_types.gen.h"
#include "fs_builtin_sources.h"

#include "core/object/script_language.h"

namespace {

struct NativeMethodReturnTypeHint {
	const char *native_class;
	const char *method;
	const char *return_type;
	const char *const *type_arguments;
};

static const char *const json_parse_to_node_type_arguments[] = {
	"JsonNode",
	nullptr,
};

static const NativeMethodReturnTypeHint native_method_return_type_hints[] = {
	{ "JSON", "parse_to_node", "JsonResult", json_parse_to_node_type_arguments },
};

} // namespace

bool FSBuiltinTypes::get_native_method_return_type_hint(
		const StringName &p_native_method_owner, const StringName &p_method,
		StringName &r_return_type, Vector<StringName> &r_type_arguments) {
	if (p_native_method_owner == StringName() || p_method == StringName()) {
		return false;
	}

	for (const NativeMethodReturnTypeHint &hint : native_method_return_type_hints) {
		if (p_method != StringName(hint.method) ||
				p_native_method_owner != StringName(hint.native_class)) {
			continue;
		}

		r_return_type = StringName(hint.return_type);
		r_type_arguments.clear();
		for (const char *const *argument = hint.type_arguments; *argument != nullptr; argument++) {
			r_type_arguments.push_back(StringName(*argument));
		}
		return true;
	}
	return false;
}

void FSBuiltinTypes::register_types() {
	for (int i = 0; i < FS_BUILTIN_SOURCE_COUNT; i++) {
		FSBuiltinSources::register_source(FS_BUILTIN_SOURCES[i].path, FS_BUILTIN_SOURCES[i].source);
	}

	const StringName language = SNAME("FoundryScript");
	ScriptServer::add_builtin_global_class(SNAME("JsonNode"), StringName(), language,
			"foundry://builtin/json_node.fs", false, false, false, true);
	ScriptServer::add_builtin_global_class(SNAME("JsonDecodeError"), SNAME("RefCounted"), language,
			"foundry://builtin/json_decode_error.fs", false, false, false, false);
	ScriptServer::add_builtin_global_class(SNAME("JsonResult"), SNAME("RefCounted"), language,
			"foundry://builtin/json_result.fs", false, false, false, false);
	ScriptServer::add_builtin_global_class(SNAME("JsonSerializable"), StringName(), language,
			"foundry://builtin/json_serializable.fs", false, false, true, false);
}

void FSBuiltinTypes::unregister_types() {
	ScriptServer::clear_builtin_global_classes();
	FSBuiltinSources::clear();
}
