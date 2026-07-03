/**************************************************************************/
/*  foundry_extension_manager.cpp                                         */
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

#include "foundry_extension_manager.h"

#include "core/extension/foundry_extension_function_loader.h"
#include "core/extension/foundry_extension_library_loader.h"
#include "core/extension/foundry_extension_special_compat_hashes.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"

FoundryExtensionManager::LoadStatus FoundryExtensionManager::_load_extension_internal(const Ref<FoundryExtension> &p_extension, bool p_first_load) {
	if (level >= 0) { // Already initialized up to some level.
		int32_t minimum_level = 0;
		if (!p_first_load) {
			minimum_level = p_extension->get_minimum_library_initialization_level();
			if (minimum_level < MIN(level, FoundryExtension::INITIALIZATION_LEVEL_SCENE)) {
				return LOAD_STATUS_NEEDS_RESTART;
			}
		}
		// Initialize up to current level.
		for (int32_t i = minimum_level; i <= level; i++) {
			p_extension->initialize_library(FoundryExtension::InitializationLevel(i));
		}
	}

	for (const KeyValue<String, String> &kv : p_extension->class_icon_paths) {
		foundry_extension_class_icon_paths[kv.key] = kv.value;
	}

	return LOAD_STATUS_OK;
}

void FoundryExtensionManager::_finish_load_extension(const Ref<FoundryExtension> &p_extension) {
#ifdef TOOLS_ENABLED
	// Signals that a new extension is loaded so FoundryScript can register new class names.
	emit_signal("extension_loaded", p_extension);
#endif

	if (startup_callback_called) {
		// Extension is loading after the startup callback has already been called,
		// so we call it now for this extension to make sure it doesn't miss it.
		if (p_extension->startup_callback) {
			p_extension->startup_callback();
		}
	}
}

FoundryExtensionManager::LoadStatus FoundryExtensionManager::_unload_extension_internal(const Ref<FoundryExtension> &p_extension) {
#ifdef TOOLS_ENABLED
	// Signals that a new extension is unloading so FoundryScript can unregister class names.
	emit_signal("extension_unloading", p_extension);
#endif

	if (!shutdown_callback_called) {
		// Extension is unloading before the shutdown callback has been called,
		// which means the engine hasn't shutdown yet but we want to make sure
		// to call the shutdown callback so it doesn't miss it.
		if (p_extension->shutdown_callback) {
			p_extension->shutdown_callback();
		}
	}

	if (level >= 0) { // Already initialized up to some level.
		// Deinitialize down from current level.
		for (int32_t i = level; i >= FoundryExtension::INITIALIZATION_LEVEL_CORE; i--) {
			p_extension->deinitialize_library(FoundryExtension::InitializationLevel(i));
		}
	}

	for (const KeyValue<String, String> &kv : p_extension->class_icon_paths) {
		foundry_extension_class_icon_paths.erase(kv.key);
	}

	// Clear main loop callbacks.
	p_extension->startup_callback = nullptr;
	p_extension->shutdown_callback = nullptr;
	p_extension->frame_callback = nullptr;

	return LOAD_STATUS_OK;
}

FoundryExtensionManager::LoadStatus FoundryExtensionManager::load_extension(const String &p_path) {
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return LOAD_STATUS_FAILED;
	}

	Ref<FoundryExtensionLibraryLoader> loader;
	loader.instantiate();
	return load_extension_with_loader(p_path, loader);
}

FoundryExtensionManager::LoadStatus FoundryExtensionManager::load_extension_from_function(const String &p_path, FoundryExtensionConstPtr<const FoundryExtensionInitializationFunction> p_init_func) {
	Ref<FoundryExtensionFunctionLoader> func_loader;
	func_loader.instantiate();
	func_loader->set_initialization_function((FoundryExtensionInitializationFunction)*p_init_func.data);
	return load_extension_with_loader(p_path, func_loader);
}

FoundryExtensionManager::LoadStatus FoundryExtensionManager::load_extension_with_loader(const String &p_path, const Ref<FoundryExtensionLoader> &p_loader) {
	DEV_ASSERT(p_loader.is_valid());

	if (foundry_extension_map.has(p_path)) {
		return LOAD_STATUS_ALREADY_LOADED;
	}

	Ref<FoundryExtension> extension;
	extension.instantiate();
	Error err = extension->open_library(p_path, p_loader);
	if (err != OK) {
		return LOAD_STATUS_FAILED;
	}

	LoadStatus status = _load_extension_internal(extension, true);
	if (status != LOAD_STATUS_OK) {
		return status;
	}

	_finish_load_extension(extension);

	extension->set_path(p_path);
	foundry_extension_map[p_path] = extension;
	return LOAD_STATUS_OK;
}

FoundryExtensionManager::LoadStatus FoundryExtensionManager::reload_extension(const String &p_path) {
#ifndef TOOLS_ENABLED
	ERR_FAIL_V_MSG(LOAD_STATUS_FAILED, "FoundryExtensions can only be reloaded in an editor build.");
#else
	ERR_FAIL_COND_V_MSG(!Engine::get_singleton()->is_extension_reloading_enabled(), LOAD_STATUS_FAILED, "FoundryExtension reloading is disabled.");

	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return LOAD_STATUS_FAILED;
	}

	if (!foundry_extension_map.has(p_path)) {
		return LOAD_STATUS_NOT_LOADED;
	}

	Ref<FoundryExtension> extension = foundry_extension_map[p_path];
	ERR_FAIL_COND_V_MSG(!extension->is_reloadable(), LOAD_STATUS_FAILED, vformat("This FoundryExtension is not marked as 'reloadable' or doesn't support reloading: %s.", p_path));

	LoadStatus status;

	extension->prepare_reload();

	// Unload library if it's open. It may not be open if the developer made a
	// change that broke loading in a previous hot-reload attempt.
	if (extension->is_library_open()) {
		status = _unload_extension_internal(extension);
		if (status != LOAD_STATUS_OK) {
			// We need to clear these no matter what.
			extension->clear_instance_bindings();
			return status;
		}

		extension->clear_instance_bindings();
		extension->close_library();
	}

	Error err = extension->open_library(p_path, extension->loader);
	if (err != OK) {
		return LOAD_STATUS_FAILED;
	}

	status = _load_extension_internal(extension, false);
	if (status != LOAD_STATUS_OK) {
		return status;
	}

	extension->finish_reload();

	// Needs to come after reload is fully finished, so all objects using
	// extension classes are in a consistent state.
	_finish_load_extension(extension);

	return LOAD_STATUS_OK;
#endif
}

FoundryExtensionManager::LoadStatus FoundryExtensionManager::unload_extension(const String &p_path) {
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return LOAD_STATUS_FAILED;
	}

	if (!foundry_extension_map.has(p_path)) {
		return LOAD_STATUS_NOT_LOADED;
	}

	Ref<FoundryExtension> extension = foundry_extension_map[p_path];

	LoadStatus status = _unload_extension_internal(extension);
	if (status != LOAD_STATUS_OK) {
		return status;
	}

	foundry_extension_map.erase(p_path);
	return LOAD_STATUS_OK;
}

bool FoundryExtensionManager::is_extension_loaded(const String &p_path) const {
	return foundry_extension_map.has(p_path);
}

Vector<String> FoundryExtensionManager::get_loaded_extensions() const {
	Vector<String> ret;
	for (const KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		ret.push_back(E.key);
	}
	return ret;
}
Ref<FoundryExtension> FoundryExtensionManager::get_extension(const String &p_path) {
	HashMap<String, Ref<FoundryExtension>>::Iterator E = foundry_extension_map.find(p_path);
	ERR_FAIL_COND_V(!E, Ref<FoundryExtension>());
	return E->value;
}

bool FoundryExtensionManager::class_has_icon_path(const String &p_class) const {
	// TODO: Check that the icon belongs to a registered class somehow.
	return foundry_extension_class_icon_paths.has(p_class);
}

String FoundryExtensionManager::class_get_icon_path(const String &p_class) const {
	// TODO: Check that the icon belongs to a registered class somehow.
	if (foundry_extension_class_icon_paths.has(p_class)) {
		return foundry_extension_class_icon_paths[p_class];
	}
	return "";
}

void FoundryExtensionManager::initialize_extensions(FoundryExtension::InitializationLevel p_level) {
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return;
	}

	ERR_FAIL_COND(int32_t(p_level) - 1 != level);
	for (KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		E.value->initialize_library(p_level);

		if (p_level == FoundryExtension::INITIALIZATION_LEVEL_EDITOR) {
			for (const KeyValue<String, String> &kv : E.value->class_icon_paths) {
				foundry_extension_class_icon_paths[kv.key] = kv.value;
			}
		}
	}
	level = p_level;
}

void FoundryExtensionManager::deinitialize_extensions(FoundryExtension::InitializationLevel p_level) {
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return;
	}

	ERR_FAIL_COND(int32_t(p_level) != level);
	for (KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		E.value->deinitialize_library(p_level);
	}
	level = int32_t(p_level) - 1;
}

#ifdef TOOLS_ENABLED
void FoundryExtensionManager::track_instance_binding(void *p_token, Object *p_object) {
	for (KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		if (E.value.ptr() == p_token) {
			if (E.value->is_reloadable()) {
				E.value->track_instance_binding(p_object);
				return;
			}
		}
	}
}

void FoundryExtensionManager::untrack_instance_binding(void *p_token, Object *p_object) {
	for (KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		if (E.value.ptr() == p_token) {
			if (E.value->is_reloadable()) {
				E.value->untrack_instance_binding(p_object);
				return;
			}
		}
	}
}

void FoundryExtensionManager::_reload_all_scripts() {
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		ScriptServer::get_language(i)->reload_all_scripts();
	}
}
#endif // TOOLS_ENABLED

void FoundryExtensionManager::load_extensions() {
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return;
	}

	Ref<FileAccess> f = FileAccess::open(FoundryExtension::get_extension_list_config_file(), FileAccess::READ);
	while (f.is_valid() && !f->eof_reached()) {
		String s = f->get_line().strip_edges();
		if (!s.is_empty()) {
			LoadStatus err = load_extension(s);
			ERR_CONTINUE_MSG(err == LOAD_STATUS_FAILED, vformat("Error loading extension: '%s'.", s));
		}
	}

	OS::get_singleton()->load_platform_foundry_extensions();
}

void FoundryExtensionManager::reload_extensions() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return;
	}
	bool reloaded = false;
	for (const KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		if (!E.value->is_reloadable()) {
			continue;
		}

		if (E.value->has_library_changed()) {
			reloaded = true;
			reload_extension(E.value->get_path());
		}
	}

	if (reloaded) {
		emit_signal("extensions_reloaded");

		// Reload all scripts to clear out old references.
		callable_mp_static(&FoundryExtensionManager::_reload_all_scripts).call_deferred();
	}
#endif
}

bool FoundryExtensionManager::ensure_extensions_loaded(const HashSet<String> &p_extensions) {
	Vector<String> extensions_added;
	Vector<String> extensions_removed;

	for (const String &E : p_extensions) {
		if (!is_extension_loaded(E)) {
			extensions_added.push_back(E);
		}
	}

	Vector<String> loaded_extensions = get_loaded_extensions();
	for (const String &loaded_extension : loaded_extensions) {
		if (!p_extensions.has(loaded_extension)) {
			// The extension may not have a .foundryextension file.
			const Ref<FoundryExtension> extension = FoundryExtensionManager::get_singleton()->get_extension(loaded_extension);
			if (!extension->get_loader()->library_exists()) {
				extensions_removed.push_back(loaded_extension);
			}
		}
	}

	String extension_list_config_file = FoundryExtension::get_extension_list_config_file();
	if (p_extensions.size()) {
		if (extensions_added.size() || extensions_removed.size()) {
			// Extensions were added or removed.
			Ref<FileAccess> f = FileAccess::open(extension_list_config_file, FileAccess::WRITE);
			for (const String &E : p_extensions) {
				f->store_line(E);
			}
		}
	} else {
		if (loaded_extensions.size() || FileAccess::exists(extension_list_config_file)) {
			// Extensions were removed.
			Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
			da->remove(extension_list_config_file);
		}
	}

	bool needs_restart = false;
	for (const String &extension : extensions_added) {
		FoundryExtensionManager::LoadStatus st = FoundryExtensionManager::get_singleton()->load_extension(extension);
		if (st == FoundryExtensionManager::LOAD_STATUS_NEEDS_RESTART) {
			needs_restart = true;
		}
	}

	for (const String &extension : extensions_removed) {
		FoundryExtensionManager::LoadStatus st = FoundryExtensionManager::get_singleton()->unload_extension(extension);
		if (st == FoundryExtensionManager::LOAD_STATUS_NEEDS_RESTART) {
			needs_restart = true;
		}
	}

#ifdef TOOLS_ENABLED
	if (extensions_added.size() || extensions_removed.size()) {
		// Emitting extensions_reloaded so EditorNode can reload Inspector and regenerate documentation.
		emit_signal("extensions_reloaded");

		// Reload all scripts to clear out old references.
		callable_mp_static(&FoundryExtensionManager::_reload_all_scripts).call_deferred();
	}
#endif

	return needs_restart;
}

void FoundryExtensionManager::startup() {
	startup_callback_called = true;

	for (const KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		const Ref<FoundryExtension> &extension = E.value;
		if (extension->startup_callback) {
			extension->startup_callback();
		}
	}
}

void FoundryExtensionManager::shutdown() {
	shutdown_callback_called = true;

	for (const KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		const Ref<FoundryExtension> &extension = E.value;
		if (extension->shutdown_callback) {
			extension->shutdown_callback();
		}
	}
}

void FoundryExtensionManager::frame() {
	for (const KeyValue<String, Ref<FoundryExtension>> &E : foundry_extension_map) {
		const Ref<FoundryExtension> &extension = E.value;
		if (extension->frame_callback) {
			extension->frame_callback();
		}
	}
}

FoundryExtensionManager *FoundryExtensionManager::get_singleton() {
	return singleton;
}

void FoundryExtensionManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_extension", "path"), &FoundryExtensionManager::load_extension);
	ClassDB::bind_method(D_METHOD("load_extension_from_function", "path", "init_func"), &FoundryExtensionManager::load_extension_from_function);
	ClassDB::bind_method(D_METHOD("reload_extension", "path"), &FoundryExtensionManager::reload_extension);
	ClassDB::bind_method(D_METHOD("unload_extension", "path"), &FoundryExtensionManager::unload_extension);
	ClassDB::bind_method(D_METHOD("is_extension_loaded", "path"), &FoundryExtensionManager::is_extension_loaded);

	ClassDB::bind_method(D_METHOD("get_loaded_extensions"), &FoundryExtensionManager::get_loaded_extensions);
	ClassDB::bind_method(D_METHOD("get_extension", "path"), &FoundryExtensionManager::get_extension);

	BIND_ENUM_CONSTANT(LOAD_STATUS_OK);
	BIND_ENUM_CONSTANT(LOAD_STATUS_FAILED);
	BIND_ENUM_CONSTANT(LOAD_STATUS_ALREADY_LOADED);
	BIND_ENUM_CONSTANT(LOAD_STATUS_NOT_LOADED);
	BIND_ENUM_CONSTANT(LOAD_STATUS_NEEDS_RESTART);

	ADD_SIGNAL(MethodInfo("extensions_reloaded"));
	ADD_SIGNAL(MethodInfo("extension_loaded", PropertyInfo(Variant::OBJECT, "extension", PROPERTY_HINT_RESOURCE_TYPE, "FoundryExtension")));
	ADD_SIGNAL(MethodInfo("extension_unloading", PropertyInfo(Variant::OBJECT, "extension", PROPERTY_HINT_RESOURCE_TYPE, "FoundryExtension")));
}

FoundryExtensionManager::FoundryExtensionManager() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
}

FoundryExtensionManager::~FoundryExtensionManager() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
