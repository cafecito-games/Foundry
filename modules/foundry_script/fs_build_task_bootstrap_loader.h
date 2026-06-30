/**************************************************************************/
/*  fs_build_task_bootstrap_loader.h                                      */
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

#include "foundry_build_task.h"
#include "foundry_script.h"

#include "core/config/foundry_build_task_registry.h"
#include "core/templates/hash_map.h"

class FSParser;

class FoundryBuildTaskBootstrapLoader {
public:
	struct LoadedProvider {
		FoundryBuildTaskRegistry::ProviderEntry descriptor;
		Ref<FoundryScript> script;
		Ref<FoundryBuildTask> instance;
	};

private:
	HashMap<String, LoadedProvider> loaded_providers;
	Vector<String> loaded_order;
	Vector<FoundryBuildTaskRegistry::Diagnostic> diagnostics;

	void _add_diagnostic(FoundryBuildTaskRegistry::DiagnosticKind p_kind,
			const FoundryBuildTaskRegistry::ProviderEntry &p_provider, const String &p_key,
			const String &p_message);
	Error _load_provider(const FoundryBuildTaskRegistry::ProviderEntry &p_provider);
	Error _validate_bootstrap_scope(const FoundryBuildTaskRegistry::ProviderEntry &p_provider,
			const FSParser &p_parser);
	Error _compile_provider(const FoundryBuildTaskRegistry::ProviderEntry &p_provider,
			Ref<FoundryScript> &r_script);
	Error _instantiate_provider(const FoundryBuildTaskRegistry::ProviderEntry &p_provider,
			const Ref<FoundryScript> &p_script, Ref<FoundryBuildTask> &r_instance);

	static String _diagnostic_message_from_parser(const FoundryBuildTaskRegistry::ProviderEntry &p_provider,
			const FSParser &p_parser, const String &p_context);
	static bool _path_is_within_root(const String &p_path, const String &p_root);
	static String _provider_allowed_root(const FoundryBuildTaskRegistry::ProviderEntry &p_provider);
	static Error _merge_error(Error p_current, Error p_candidate);

public:
	void clear();

	Error load_registered_providers(const FoundryBuildTaskRegistry &p_registry);
	Error load_project_bootstrap_providers(const String &p_project_config_path = "res://project.foundry");
	Error load_provider_schema(const String &p_provider_id, Ref<FoundryBuildTaskConfigSchema> &r_schema);

	bool has_loaded_provider(const String &p_provider_id) const;
	const LoadedProvider *get_loaded_provider(const String &p_provider_id) const;

	const Vector<String> &get_loaded_order() const { return loaded_order; }
	const Vector<FoundryBuildTaskRegistry::Diagnostic> &get_diagnostics() const { return diagnostics; }
};
