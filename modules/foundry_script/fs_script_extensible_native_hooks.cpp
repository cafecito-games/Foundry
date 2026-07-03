/**************************************************************************/
/*  fs_script_extensible_native_hooks.cpp                                 */
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

#include "fs_script_extensible_native_hooks.h"

#include "core/object/class_db.h"

namespace {

struct ScriptExtensibleNativeHook {
	const char *native_base;
	const char *method_name;
};

static const ScriptExtensibleNativeHook script_extensible_native_hooks[] = {
	{ "FoundryBuildTask", "get_config_schema" },
	{ "FoundryBuildTask", "run" },
	{ "ScriptTestRunner", "run" },
};

static const ScriptExtensibleNativeHook flexible_async_native_hooks[] = {
	{ "ScriptTestRunner", "run" },
};

} // namespace

bool FSScriptExtensibleNativeHooks::is_allowed_override(
		const StringName &p_native_base, const StringName &p_method_name) {
	if (p_native_base == StringName() || p_method_name == StringName()) {
		return false;
	}

	for (const ScriptExtensibleNativeHook &hook : script_extensible_native_hooks) {
		if (p_method_name == StringName(hook.method_name) &&
				ClassDB::is_parent_class(p_native_base, StringName(hook.native_base))) {
			return true;
		}
	}
	return false;
}

bool FSScriptExtensibleNativeHooks::allows_async_override_of_sync_hook(
		const StringName &p_native_base, const StringName &p_method_name) {
	if (p_native_base == StringName() || p_method_name == StringName()) {
		return false;
	}

	for (const ScriptExtensibleNativeHook &hook : flexible_async_native_hooks) {
		if (p_method_name == StringName(hook.method_name) &&
				ClassDB::is_parent_class(p_native_base, StringName(hook.native_base))) {
			return true;
		}
	}
	return false;
}

void FSScriptExtensibleNativeHooks::collect_allowed_overrides(
		const StringName &p_native_base, List<StringName> &r_method_names) {
	if (p_native_base == StringName()) {
		return;
	}

	for (const ScriptExtensibleNativeHook &hook : script_extensible_native_hooks) {
		if (ClassDB::is_parent_class(p_native_base, StringName(hook.native_base))) {
			r_method_names.push_back(StringName(hook.method_name));
		}
	}
}
