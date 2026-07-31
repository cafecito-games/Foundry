/**************************************************************************/
/*  script_language.cpp                                                   */
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

#include "script_language.h"

#include "core/config/project_settings.h"
#include "core/core_bind.h"
#include "core/debugger/engine_debugger.h"
#include "core/debugger/script_debugger.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/templates/sort_array.h"

ScriptLanguage *ScriptServer::_languages[MAX_LANGUAGES];
int ScriptServer::_language_count = 0;
bool ScriptServer::languages_ready = false;
Mutex ScriptServer::languages_mutex;
thread_local bool ScriptServer::thread_entered = false;

bool ScriptServer::scripting_enabled = true;
bool ScriptServer::reload_scripts_on_save = false;
ScriptEditRequestFunction ScriptServer::edit_request_func = nullptr;

// These need to be the last static variables in this file, since we're exploiting the reverse-order destruction of static variables.
static bool is_program_exiting = false;
struct ProgramExitGuard {
	~ProgramExitGuard() {
		is_program_exiting = true;
	}
};
static ProgramExitGuard program_exit_guard;

void Script::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_POSTINITIALIZE: {
			if (EngineDebugger::is_active()) {
				callable_mp(this, &Script::_set_debugger_break_language).call_deferred();
			}
		} break;
	}
}

Variant Script::_get_property_default_value(const StringName &p_property) {
	Variant ret;
	get_property_default_value(p_property, ret);
	return ret;
}

TypedArray<Dictionary> Script::_get_script_property_list() {
	TypedArray<Dictionary> ret;
	List<PropertyInfo> list;
	get_script_property_list(&list);
	for (const PropertyInfo &E : list) {
		ret.append(E.operator Dictionary());
	}
	return ret;
}

TypedArray<Dictionary> Script::_get_script_method_list() {
	TypedArray<Dictionary> ret;
	List<MethodInfo> list;
	get_script_method_list(&list);
	for (const MethodInfo &E : list) {
		ret.append(E.operator Dictionary());
	}
	return ret;
}

TypedArray<Dictionary> Script::_get_script_signal_list() {
	TypedArray<Dictionary> ret;
	List<MethodInfo> list;
	get_script_signal_list(&list);
	for (const MethodInfo &E : list) {
		ret.append(E.operator Dictionary());
	}
	return ret;
}

TypedArray<StringName> Script::_get_script_trait_list() {
	TypedArray<StringName> ret;
	List<StringName> list;
	get_script_trait_list(&list);
	for (const StringName &E : list) {
		ret.append(E);
	}
	return ret;
}

Dictionary Script::_get_script_constant_map() {
	Dictionary ret;
	HashMap<StringName, Variant> map;
	get_constants(&map);
	for (const KeyValue<StringName, Variant> &E : map) {
		ret[E.key] = E.value;
	}
	return ret;
}

void Script::_set_debugger_break_language() {
	if (EngineDebugger::is_active()) {
		EngineDebugger::get_script_debugger()->set_break_language(get_language());
	}
}

int Script::get_script_method_argument_count(const StringName &p_method, bool *r_is_valid) const {
	MethodInfo mi = get_method_info(p_method);

	if (mi == MethodInfo()) {
		if (r_is_valid) {
			*r_is_valid = false;
		}
		return 0;
	}

	if (r_is_valid) {
		*r_is_valid = true;
	}
	return mi.arguments.size();
}

bool Script::has_script_trait(const StringName &p_trait) const {
	List<StringName> traits;
	get_script_trait_list(&traits);
	for (const StringName &trait : traits) {
		if (trait == p_trait) {
			return true;
		}
	}
	return false;
}

#ifdef TOOLS_ENABLED

PropertyInfo Script::get_class_category() const {
	String path = get_path();
	String scr_name;

	if (is_built_in()) {
		if (get_name().is_empty()) {
			scr_name = TTR("Built-in script");
		} else {
			scr_name = vformat("%s (%s)", get_name(), TTR("Built-in"));
		}
	} else {
		if (get_name().is_empty()) {
			scr_name = path.get_file();
		} else {
			scr_name = get_name();
		}
	}

	return PropertyInfo(Variant::NIL, scr_name, PROPERTY_HINT_NONE, path, PROPERTY_USAGE_CATEGORY);
}

#endif // TOOLS_ENABLED

void Script::_bind_methods() {
	ClassDB::bind_method(D_METHOD("can_instantiate"), &Script::can_instantiate);
	//ClassDB::bind_method(D_METHOD("instance_create","base_object"),&Script::instance_create);
	ClassDB::bind_method(D_METHOD("instance_has", "base_object"), &Script::instance_has);
	ClassDB::bind_method(D_METHOD("has_source_code"), &Script::has_source_code);
	ClassDB::bind_method(D_METHOD("get_source_code"), &Script::get_source_code);
	ClassDB::bind_method(D_METHOD("set_source_code", "source"), &Script::set_source_code);
	ClassDB::bind_method(D_METHOD("reload", "keep_state"), &Script::reload, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_base_script"), &Script::get_base_script);
	ClassDB::bind_method(D_METHOD("get_instance_base_type"), &Script::get_instance_base_type);

	ClassDB::bind_method(D_METHOD("get_global_name"), &Script::get_global_name);

	ClassDB::bind_method(D_METHOD("has_script_signal", "signal_name"), &Script::has_script_signal);
	ClassDB::bind_method(D_METHOD("has_script_trait", "trait_name"), &Script::has_script_trait);

	ClassDB::bind_method(D_METHOD("get_script_property_list"), &Script::_get_script_property_list);
	ClassDB::bind_method(D_METHOD("get_script_method_list"), &Script::_get_script_method_list);
	ClassDB::bind_method(D_METHOD("get_script_signal_list"), &Script::_get_script_signal_list);
	ClassDB::bind_method(D_METHOD("get_script_trait_list"), &Script::_get_script_trait_list);
	ClassDB::bind_method(D_METHOD("get_script_constant_map"), &Script::_get_script_constant_map);
	ClassDB::bind_method(D_METHOD("get_property_default_value", "property"), &Script::_get_property_default_value);

	ClassDB::bind_method(D_METHOD("is_tool"), &Script::is_tool);
	ClassDB::bind_method(D_METHOD("is_abstract"), &Script::is_abstract);

	ClassDB::bind_method(D_METHOD("get_rpc_config"), &Script::_get_rpc_config_bind);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source_code", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_source_code", "get_source_code");
}

void Script::reload_from_file() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint() && is_tool()) {
		get_language()->reload_tool_script(this, true);
	} else {
		Array scripts = { this };
		get_language()->reload_scripts(scripts, true);
	}
#else
	Resource::reload_from_file();
#endif
}

void ScriptServer::set_scripting_enabled(bool p_enabled) {
	scripting_enabled = p_enabled;
}

bool ScriptServer::is_scripting_enabled() {
	return scripting_enabled;
}

ScriptLanguage *ScriptServer::get_language(int p_idx) {
	MutexLock lock(languages_mutex);
	ERR_FAIL_INDEX_V(p_idx, _language_count, nullptr);
	return _languages[p_idx];
}

ScriptLanguage *ScriptServer::get_language_for_extension(const String &p_extension) {
	MutexLock lock(languages_mutex);

	for (int i = 0; i < _language_count; i++) {
		if (_languages[i] && _languages[i]->get_extension() == p_extension) {
			return _languages[i];
		}
	}

	return nullptr;
}

Error ScriptServer::register_language(ScriptLanguage *p_language) {
	MutexLock lock(languages_mutex);
	ERR_FAIL_NULL_V(p_language, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(_language_count >= MAX_LANGUAGES, ERR_UNAVAILABLE, "Script languages limit has been reach, cannot register more.");
	for (int i = 0; i < _language_count; i++) {
		const ScriptLanguage *other_language = _languages[i];
		ERR_FAIL_COND_V_MSG(other_language->get_extension() == p_language->get_extension(), ERR_ALREADY_EXISTS, vformat("A script language with extension '%s' is already registered.", p_language->get_extension()));
		ERR_FAIL_COND_V_MSG(other_language->get_name() == p_language->get_name(), ERR_ALREADY_EXISTS, vformat("A script language with name '%s' is already registered.", p_language->get_name()));
		ERR_FAIL_COND_V_MSG(other_language->get_type() == p_language->get_type(), ERR_ALREADY_EXISTS, vformat("A script language with type '%s' is already registered.", p_language->get_type()));
	}
	_languages[_language_count++] = p_language;

	// Make sure the new language is initialized in case languages have already been initialized before
	// This happens when importing the FoundryExtension for the first time in the editor
	if (languages_ready) {
		p_language->init();
	}

	return OK;
}

Error ScriptServer::unregister_language(const ScriptLanguage *p_language) {
	MutexLock lock(languages_mutex);

	for (int i = 0; i < _language_count; i++) {
		if (_languages[i] == p_language) {
			_language_count--;
			if (i < _language_count) {
				SWAP(_languages[i], _languages[_language_count]);
			}
			return OK;
		}
	}
	return ERR_DOES_NOT_EXIST;
}

void ScriptServer::reload_global_classes_from_project() {
	global_classes_clear();

	Array script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	for (const Variant &script_class : script_classes) {
		Dictionary c = script_class;
		if (!c.has("class") || !c.has("language") || !c.has("path") || !c.has("base") || !c.has("is_abstract") || !c.has("is_tool")) {
			continue;
		}
		// `is_trait` and `is_enum` were added later, so they may be absent in older caches.
		const bool is_trait = c.has("is_trait") && c["is_trait"];
		const bool is_enum = c.has("is_enum") && c["is_enum"];
		add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"], is_trait, is_enum);
	}

	reload_global_conformances_from_project(true);
}

void ScriptServer::reload_global_conformances_from_project(bool p_clear_existing) {
	// An exported project never rescans its scripts, so the cache written at export time is the only
	// thing that can tell a language which files declare conformances. Without it a namespace import
	// would resolve in the editor and fail in the exported game.
	if (p_clear_existing) {
		MutexLock lock(languages_mutex);
		for (int i = 0; i < _language_count; i++) {
			if (_languages[i] != nullptr) {
				_languages[i]->clear_indexed_conformances();
			}
		}
	}

	Array conformances = ProjectSettings::get_singleton()->get_global_conformance_list();
	for (const Variant &conformance : conformances) {
		Dictionary entry = conformance;
		if (!entry.has("path") || !entry.has("language")) {
			continue;
		}
		const String language_name = entry["language"];
		MutexLock lock(languages_mutex);
		for (int i = 0; i < _language_count; i++) {
			if (_languages[i] != nullptr && _languages[i]->get_name() == language_name) {
				_languages[i]->add_indexed_conformance(entry["path"], entry.get("namespace", String()));
				break;
			}
		}
	}
}

Array ScriptServer::get_global_conformances() {
	Array conformances;
	MutexLock lock(languages_mutex);
	for (int i = 0; i < _language_count; i++) {
		if (_languages[i] == nullptr) {
			continue;
		}
		Array language_conformances;
		_languages[i]->get_indexed_conformances(language_conformances);
		for (const Variant &entry : language_conformances) {
			Dictionary conformance = entry;
			conformance["language"] = _languages[i]->get_name();
			conformances.push_back(conformance);
		}
	}
	return conformances;
}

void ScriptServer::init_languages() {
	reload_global_classes_from_project();

	HashSet<ScriptLanguage *> langs_to_init;
	{
		MutexLock lock(languages_mutex);
		for (int i = 0; i < _language_count; i++) {
			if (_languages[i]) {
				langs_to_init.insert(_languages[i]);
			}
		}
	}

	for (ScriptLanguage *E : langs_to_init) {
		E->init();
	}

	{
		MutexLock lock(languages_mutex);
		languages_ready = true;
	}
}

void ScriptServer::finish_languages() {
	HashSet<ScriptLanguage *> langs_to_finish;

	{
		MutexLock lock(languages_mutex);
		for (int i = 0; i < _language_count; i++) {
			if (_languages[i]) {
				langs_to_finish.insert(_languages[i]);
			}
		}
	}

	for (ScriptLanguage *E : langs_to_finish) {
		if (CoreBind::OS::get_singleton()) {
			CoreBind::OS::get_singleton()->remove_script_loggers(E); // Unregister loggers using this script language.
		}
		E->finish();
	}

	{
		MutexLock lock(languages_mutex);
		languages_ready = false;
	}

	global_classes_clear();
}

bool ScriptServer::are_languages_initialized() {
	MutexLock lock(languages_mutex);
	return languages_ready;
}

bool ScriptServer::thread_is_entered() {
	return thread_entered;
}

void ScriptServer::set_reload_scripts_on_save(bool p_enable) {
	reload_scripts_on_save = p_enable;
}

bool ScriptServer::is_reload_scripts_on_save_enabled() {
	return reload_scripts_on_save;
}

void ScriptServer::thread_enter() {
	if (thread_entered) {
		return;
	}

	MutexLock lock(languages_mutex);
	if (!languages_ready) {
		return;
	}
	for (int i = 0; i < _language_count; i++) {
		_languages[i]->thread_enter();
	}

	thread_entered = true;
}

void ScriptServer::thread_exit() {
	if (!thread_entered) {
		return;
	}

	MutexLock lock(languages_mutex);
	if (!languages_ready) {
		return;
	}
	for (int i = 0; i < _language_count; i++) {
		_languages[i]->thread_exit();
	}

	thread_entered = false;
}

HashMap<StringName, ScriptServer::GlobalScriptClass> ScriptServer::global_classes;
uint64_t ScriptServer::global_classes_version = 0;
HashMap<StringName, Vector<StringName>> ScriptServer::inheriters_cache;
bool ScriptServer::inheriters_cache_dirty = true;

void ScriptServer::global_classes_clear() {
	global_classes.clear();
	inheriters_cache.clear();
	global_classes_version++;
}

void ScriptServer::get_global_class_name_parts(const StringName &p_class, StringName *r_class_name,
		String *r_namespace_name) {
	const String qualified_name = p_class;
	// Namespaces are dot-joined; class identifiers cannot contain dots.
	const int namespace_separator = qualified_name.rfind(".");
	if (namespace_separator == -1) {
		if (r_class_name) {
			*r_class_name = p_class;
		}
		if (r_namespace_name) {
			*r_namespace_name = String();
		}
		return;
	}

	if (r_class_name) {
		*r_class_name = qualified_name.substr(namespace_separator + 1);
	}
	if (r_namespace_name) {
		*r_namespace_name = qualified_name.substr(0, namespace_separator);
	}
}

void ScriptServer::add_global_class(const StringName &p_class, const StringName &p_base, const StringName &p_language, const String &p_path, bool p_is_abstract, bool p_is_tool, bool p_is_trait, bool p_is_enum) {
	ERR_FAIL_COND_MSG(p_class == p_base || (global_classes.has(p_base) && get_global_class_native_base(p_base) == p_class), "Cyclic inheritance in script class.");

	GlobalScriptClass *existing = global_classes.getptr(p_class);
	if (existing) {
		// Update an existing class (only set dirty if something changed).
		if (existing->base != p_base ||
				existing->path != p_path ||
				existing->language != p_language ||
				existing->is_abstract != p_is_abstract ||
				existing->is_tool != p_is_tool ||
				existing->is_trait != p_is_trait ||
				existing->is_enum != p_is_enum) {
			existing->base = p_base;
			existing->path = p_path;
			existing->language = p_language;
			existing->is_abstract = p_is_abstract;
			existing->is_tool = p_is_tool;
			existing->is_trait = p_is_trait;
			existing->is_enum = p_is_enum;
			inheriters_cache_dirty = true;
			global_classes_version++;
		}
	} else {
		// Add new class.
		GlobalScriptClass g;
		g.language = p_language;
		g.path = p_path;
		g.base = p_base;
		g.is_abstract = p_is_abstract;
		g.is_tool = p_is_tool;
		g.is_trait = p_is_trait;
		g.is_enum = p_is_enum;
		global_classes[p_class] = g;
		inheriters_cache_dirty = true;
		global_classes_version++;
	}
}

void ScriptServer::remove_global_class(const StringName &p_class) {
	if (global_classes.erase(p_class)) {
		inheriters_cache_dirty = true;
		global_classes_version++;
	}
}

void ScriptServer::get_inheriters_list(const StringName &p_base_type, List<StringName> *r_classes) {
	if (inheriters_cache_dirty) {
		inheriters_cache.clear();
		for (const KeyValue<StringName, GlobalScriptClass> &K : global_classes) {
			if (!inheriters_cache.has(K.value.base)) {
				inheriters_cache[K.value.base] = Vector<StringName>();
			}
			inheriters_cache[K.value.base].push_back(K.key);
		}
		for (KeyValue<StringName, Vector<StringName>> &K : inheriters_cache) {
			K.value.sort_custom<StringName::AlphCompare>();
		}
		inheriters_cache_dirty = false;
	}

	if (!inheriters_cache.has(p_base_type)) {
		return;
	}

	const Vector<StringName> &v = inheriters_cache[p_base_type];
	for (int i = 0; i < v.size(); i++) {
		r_classes->push_back(v[i]);
	}
}

void ScriptServer::get_indirect_inheriters_list(const StringName &p_base_type, List<StringName> *r_classes) {
	List<StringName> direct_inheritors;
	get_inheriters_list(p_base_type, &direct_inheritors);
	for (const StringName &inheritor : direct_inheritors) {
		r_classes->push_back(inheritor);
		get_indirect_inheriters_list(inheritor, r_classes);
	}
}

void ScriptServer::remove_global_class_by_path(const String &p_path) {
	for (const KeyValue<StringName, GlobalScriptClass> &kv : global_classes) {
		if (kv.value.path == p_path) {
			global_classes.erase(kv.key);
			inheriters_cache_dirty = true;
			global_classes_version++;
			return;
		}
	}
}

uint64_t ScriptServer::get_global_class_cache_version() {
	return global_classes_version;
}

bool ScriptServer::is_global_class(const StringName &p_class) {
	return global_classes.has(p_class);
}

StringName ScriptServer::get_global_class_language(const StringName &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), StringName());
	return global_classes[p_class].language;
}

String ScriptServer::get_global_class_path(const String &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), String());
	return global_classes[p_class].path;
}

StringName ScriptServer::get_global_class_base(const String &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), String());
	return global_classes[p_class].base;
}

StringName ScriptServer::get_global_class_native_base(const String &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), String());
	String base = global_classes[p_class].base;
	while (global_classes.has(base)) {
		base = global_classes[base].base;
	}
	return base;
}

bool ScriptServer::is_global_class_abstract(const String &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), false);
	return global_classes[p_class].is_abstract;
}

bool ScriptServer::is_global_class_tool(const String &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), false);
	return global_classes[p_class].is_tool;
}

bool ScriptServer::is_global_class_trait(const String &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), false);
	return global_classes[p_class].is_trait;
}

bool ScriptServer::is_global_class_enum(const String &p_class) {
	ERR_FAIL_COND_V(!global_classes.has(p_class), false);
	return global_classes[p_class].is_enum;
}

// This function only sorts items added by this function.
// If `r_global_classes` is not empty before calling and a global sort is needed, caller must handle that separately.
void ScriptServer::get_global_class_list(LocalVector<StringName> &r_global_classes) {
	if (global_classes.is_empty()) {
		return;
	}
	r_global_classes.reserve(r_global_classes.size() + global_classes.size());
	for (const KeyValue<StringName, GlobalScriptClass> &global_class : global_classes) {
		r_global_classes.push_back(global_class.key);
	}
	SortArray<StringName, StringName::AlphCompare> sorter;
	sorter.sort(&r_global_classes[r_global_classes.size() - global_classes.size()], global_classes.size());
}

void ScriptServer::save_global_classes() {
	Dictionary class_icons;

	Array script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	for (const Variant &script_class : script_classes) {
		Dictionary d = script_class;
		if (!d.has("name") || !d.has("icon")) {
			continue;
		}
		class_icons[d["name"]] = d["icon"];
	}

	LocalVector<StringName> gc;
	get_global_class_list(gc);
	Array gcarr;
	for (const StringName &class_name : gc) {
		const GlobalScriptClass &global_class = global_classes[class_name];
		Dictionary d;
		d["class"] = class_name;
		d["language"] = global_class.language;
		d["path"] = global_class.path;
		d["base"] = global_class.base;
		d["icon"] = class_icons.get(class_name, "");
		d["is_abstract"] = global_class.is_abstract;
		d["is_tool"] = global_class.is_tool;
		d["is_trait"] = global_class.is_trait;
		d["is_enum"] = global_class.is_enum;
		gcarr.push_back(d);
	}
	ProjectSettings::get_singleton()->store_global_class_list(gcarr);
	ProjectSettings::get_singleton()->store_global_conformance_list(get_global_conformances());
}

// Recursively collects script files under p_directory_path, mirroring the directories the editor
// file-system scan descends into: hidden entries, symlinked directories, nested projects,
// `.fsignore`-marked trees, and the project data directory are skipped, so the scan registers the
// same set of global classes the editor would persist to the class cache.
static void scan_script_files_in_directory(const String &p_directory_path, const HashMap<String, ScriptLanguage *> &p_language_by_extension, const String &p_project_data_path, Vector<String> &r_files) {
	Ref<DirAccess> dir = DirAccess::open(p_directory_path);
	if (dir.is_null() || dir->list_dir_begin() != OK) {
		return;
	}

	Vector<String> subdirectories;
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry.begins_with(".") || dir->current_is_hidden()) {
			continue;
		}

		const String child_path = p_directory_path.path_join(entry);

		// Never follow directory symlinks: a link outside the project or back into an ancestor
		// would let the scan escape the project root or recurse forever.
		if (dir->is_link(child_path)) {
			continue;
		}

		if (dir->current_is_dir()) {
			subdirectories.push_back(child_path);
		} else if (p_language_by_extension.has(entry.get_extension().to_lower())) {
			r_files.push_back(child_path);
		}
	}
	dir->list_dir_end();

	for (const String &subdirectory : subdirectories) {
		if (!p_project_data_path.is_empty() && subdirectory == p_project_data_path) {
			continue;
		}
		// A directory holding its own project file is a separate project, never part of this one.
		if (FileAccess::exists(subdirectory.path_join("project.foundry"))) {
			continue;
		}
		// Honor the editor's ignore marker so vendored/generated trees stay excluded.
		if (FileAccess::exists(subdirectory.path_join(".fsignore"))) {
			continue;
		}
		scan_script_files_in_directory(subdirectory, p_language_by_extension, p_project_data_path, r_files);
	}
}

void ScriptServer::scan_global_classes(const String &p_root) {
	// Rebuilds the in-memory global class table for p_root from what is actually on disk, without
	// requiring the editor file system and without writing `global_script_class_cache.cfg` back.
	// This is the fallback for processes that never construct EditorFileSystem (e.g.
	// `--headless --script` runs), where the cache file on disk may be missing or stale.
	HashMap<String, ScriptLanguage *> language_by_extension;
	for (int i = 0; i < get_language_count(); i++) {
		ScriptLanguage *language = get_language(i);
		const String extension = language->get_extension().to_lower();
		if (!extension.is_empty()) {
			language_by_extension[extension] = language;
		}
	}
	if (language_by_extension.is_empty()) {
		return;
	}

	String project_data_path;
	if (ProjectSettings::get_singleton()) {
		project_data_path = ProjectSettings::get_singleton()->get_project_data_path();
	}

	Vector<String> files;
	scan_script_files_in_directory(p_root, language_by_extension, project_data_path, files);
	// Sort so a class name declared by multiple files always resolves to the same file,
	// independent of filesystem enumeration order.
	files.sort();

	// The scan is the source of truth for everything it covers: drop entries under the scanned
	// root owned by a scanned language, so classes whose file was deleted, renamed, or moved since
	// the cache was written do not linger. Files still declaring their class are re-added below.
	const String root_prefix = p_root.ends_with("/") ? p_root : p_root + "/";
	LocalVector<StringName> stale_classes;
	for (const KeyValue<StringName, GlobalScriptClass> &kv : global_classes) {
		if (!kv.value.path.begins_with(root_prefix)) {
			continue;
		}
		const ScriptLanguage *const *language = language_by_extension.getptr(kv.value.path.get_extension().to_lower());
		if (language && (*language)->get_name() == String(kv.value.language)) {
			stale_classes.push_back(kv.key);
		}
	}
	for (const StringName &stale_class : stale_classes) {
		remove_global_class(stale_class);
	}

	// Same reconciliation for declarations that export no global class (custom annotations,
	// retroactive conformances): a file deleted since the last scan must stop being advertised, or
	// an import would keep resolving to a path that no longer exists. Re-indexing below restores
	// every file the scan still finds.
	for (const KeyValue<String, ScriptLanguage *> &language_entry : language_by_extension) {
		language_entry.value->clear_global_declaration_index_under(root_prefix);
	}

	for (const String &file : files) {
		ScriptLanguage *language = language_by_extension[file.get_extension().to_lower()];

		// Refresh the language's cross-file declaration indexes even for files that declare no
		// global class, so annotation-only and conformance-only libraries stay resolvable (mirrors
		// the editor scan, see EditorFileSystem::_register_global_class_script).
		language->update_global_declaration_index(file, file);

		String base_type;
		bool is_abstract = false;
		bool is_tool = false;
		bool is_trait = false;
		bool is_enum = false;
		const String class_name = language->get_global_class_name(file, &base_type, nullptr, &is_abstract, &is_tool, &is_trait, &is_enum);
		if (class_name.is_empty()) {
			continue;
		}

		if (is_global_class(class_name) && get_global_class_path(class_name) != file) {
			ERR_PRINT(vformat(R"(Global script class "%s" from "%s" collides with existing script class from "%s".)", class_name, file, get_global_class_path(class_name)));
			continue;
		}

		add_global_class(class_name, base_type, language->get_name(), file, is_abstract, is_tool, is_trait, is_enum);
	}

	print_verbose(vformat("ScriptServer: Scanned %d script file(s) under \"%s\" for global classes (%d registered).", files.size(), p_root, global_classes.size()));
}

Vector<Ref<ScriptBacktrace>> ScriptServer::capture_script_backtraces(bool p_include_variables) {
	if (is_program_exiting) {
		return Vector<Ref<ScriptBacktrace>>();
	}

	MutexLock lock(languages_mutex);
	if (!languages_ready) {
		return Vector<Ref<ScriptBacktrace>>();
	}

	Vector<Ref<ScriptBacktrace>> result;
	result.resize(_language_count);
	for (int i = 0; i < _language_count; i++) {
		result.write[i].instantiate(_languages[i], p_include_variables);
	}

	return result;
}

////////////////////

void ScriptLanguage::get_core_type_words(List<String> *p_core_type_words) const {
	p_core_type_words->push_back("String");
	p_core_type_words->push_back("Vector2");
	p_core_type_words->push_back("Vector2i");
	p_core_type_words->push_back("Rect2");
	p_core_type_words->push_back("Rect2i");
	p_core_type_words->push_back("Vector3");
	p_core_type_words->push_back("Vector3i");
	p_core_type_words->push_back("Transform2D");
	p_core_type_words->push_back("Vector4");
	p_core_type_words->push_back("Vector4i");
	p_core_type_words->push_back("Plane");
	p_core_type_words->push_back("Quaternion");
	p_core_type_words->push_back("AABB");
	p_core_type_words->push_back("Basis");
	p_core_type_words->push_back("Transform3D");
	p_core_type_words->push_back("Projection");
	p_core_type_words->push_back("Color");
	p_core_type_words->push_back("StringName");
	p_core_type_words->push_back("NodePath");
	p_core_type_words->push_back("RID");
	p_core_type_words->push_back("Callable");
	p_core_type_words->push_back("Signal");
	p_core_type_words->push_back("Dictionary");
	p_core_type_words->push_back("Array");
	p_core_type_words->push_back("PackedByteArray");
	p_core_type_words->push_back("PackedInt32Array");
	p_core_type_words->push_back("PackedInt64Array");
	p_core_type_words->push_back("PackedFloat32Array");
	p_core_type_words->push_back("PackedFloat64Array");
	p_core_type_words->push_back("PackedStringArray");
	p_core_type_words->push_back("PackedVector2Array");
	p_core_type_words->push_back("PackedVector3Array");
	p_core_type_words->push_back("PackedColorArray");
	p_core_type_words->push_back("PackedVector4Array");
}

void ScriptLanguage::frame() {
}

TypedArray<int> ScriptLanguage::CodeCompletionOption::get_option_characteristics(const String &p_base) {
	// Return characteristics of the match found by order of importance.
	// Matches will be ranked by a lexicographical order on the vector returned by this function.
	// The lower values indicate better matches and that they should go before in the order of appearance.
	if (!matches_dirty) {
		return charac;
	}
	charac.clear();
	// Ensure base is not empty and at the same time that matches is not empty too.
	if (p_base.length() == 0) {
		matches_dirty = false;
		charac.push_back(location);
		return charac;
	}
	charac.push_back(matches.size());
	charac.push_back((matches[0].first == 0) ? 0 : 1);
	const char32_t *target_char = &p_base[0];
	int bad_case = 0;
	for (const Pair<int, int> &match_segment : matches) {
		const char32_t *string_to_complete_char = &display[match_segment.first];
		for (int j = 0; j < match_segment.second; j++, string_to_complete_char++, target_char++) {
			if (*string_to_complete_char != *target_char) {
				bad_case++;
			}
		}
	}
	charac.push_back(bad_case);
	charac.push_back(location);
	charac.push_back(matches[0].first);
	matches_dirty = false;
	return charac;
}

void ScriptLanguage::CodeCompletionOption::clear_characteristics() {
	charac = TypedArray<int>();
}

TypedArray<int> ScriptLanguage::CodeCompletionOption::get_option_cached_characteristics() const {
	// Only returns the cached value and warns if it was not updated since the last change of matches.
	if (matches_dirty) {
		WARN_PRINT("Characteristics are not up to date.");
	}

	return charac;
}

void ScriptLanguage::_bind_methods() {
	BIND_ENUM_CONSTANT(SCRIPT_NAME_CASING_AUTO);
	BIND_ENUM_CONSTANT(SCRIPT_NAME_CASING_PASCAL_CASE);
	BIND_ENUM_CONSTANT(SCRIPT_NAME_CASING_SNAKE_CASE);
	BIND_ENUM_CONSTANT(SCRIPT_NAME_CASING_KEBAB_CASE);
	BIND_ENUM_CONSTANT(SCRIPT_NAME_CASING_CAMEL_CASE);
}

bool PlaceHolderScriptInstance::set(const StringName &p_name, const Variant &p_value) {
	if (script->is_placeholder_fallback_enabled()) {
		return false;
	}

	if (values.has(p_name)) {
		Variant defval;
		if (script->get_property_default_value(p_name, defval)) {
			// The evaluate function ensures that a NIL variant is equal to e.g. an empty Resource.
			// Simply doing defval == p_value does not do this.
			if (Variant::evaluate(Variant::OP_EQUAL, defval, p_value)) {
				values.erase(p_name);
				return true;
			}
		}
		values[p_name] = p_value;
		return true;
	} else {
		Variant defval;
		if (script->get_property_default_value(p_name, defval)) {
			if (Variant::evaluate(Variant::OP_NOT_EQUAL, defval, p_value)) {
				values[p_name] = p_value;
			}
			return true;
		}
	}
	return false;
}

bool PlaceHolderScriptInstance::get(const StringName &p_name, Variant &r_ret) const {
	if (values.has(p_name)) {
		r_ret = values[p_name];
		return true;
	}

	if (constants.has(p_name)) {
		r_ret = constants[p_name];
		return true;
	}

	if (!script->is_placeholder_fallback_enabled()) {
		Variant defval;
		if (script->get_property_default_value(p_name, defval)) {
			r_ret = defval;
			return true;
		}
	}

	return false;
}

void PlaceHolderScriptInstance::get_property_list(List<PropertyInfo> *p_properties) const {
	if (script->is_placeholder_fallback_enabled()) {
		for (const PropertyInfo &E : properties) {
			p_properties->push_back(E);
		}
	} else {
		for (const PropertyInfo &E : properties) {
			PropertyInfo pinfo = E;
			p_properties->push_back(E);
		}
	}
}

Variant::Type PlaceHolderScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	if (values.has(p_name)) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return values[p_name].get_type();
	}

	if (constants.has(p_name)) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return constants[p_name].get_type();
	}

	if (r_is_valid) {
		*r_is_valid = false;
	}

	return Variant::NIL;
}

void PlaceHolderScriptInstance::get_method_list(List<MethodInfo> *p_list) const {
	if (script->is_placeholder_fallback_enabled()) {
		return;
	}

	if (script.is_valid()) {
		script->get_script_method_list(p_list);
	}
}

bool PlaceHolderScriptInstance::has_method(const StringName &p_method) const {
	if (script->is_placeholder_fallback_enabled()) {
		return false;
	}

	if (script.is_valid()) {
		Ref<Script> scr = script;
		while (scr.is_valid()) {
			if (scr->has_method(p_method)) {
				return true;
			}
			scr = scr->get_base_script();
		}
	}
	return false;
}

Variant PlaceHolderScriptInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
#if TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		return String("Attempt to call a method on a placeholder instance. Check if the script is in tool mode.");
	} else {
		return String("Attempt to call a method on a placeholder instance. Probably a bug, please report.");
	}
#else
	return Variant();
#endif // TOOLS_ENABLED
}

void PlaceHolderScriptInstance::update(const List<PropertyInfo> &p_properties, const HashMap<StringName, Variant> &p_values) {
	HashSet<StringName> new_values;
	for (const PropertyInfo &E : p_properties) {
		if (E.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
			continue;
		}

		StringName n = E.name;
		new_values.insert(n);

		if (!values.has(n) || (E.type != Variant::NIL && values[n].get_type() != E.type)) {
			if (p_values.has(n)) {
				values[n] = p_values[n];
			}
		}
	}

	properties = p_properties;
	List<StringName> to_remove;

	for (KeyValue<StringName, Variant> &E : values) {
		if (!new_values.has(E.key)) {
			to_remove.push_back(E.key);
		}

		Variant defval;
		if (script->get_property_default_value(E.key, defval)) {
			//remove because it's the same as the default value
			if (defval == E.value) {
				to_remove.push_back(E.key);
			}
		}
	}

	while (to_remove.size()) {
		values.erase(to_remove.front()->get());
		to_remove.pop_front();
	}

	if (owner && owner->get_script_instance() == this) {
		owner->notify_property_list_changed();
	}
	//change notify

	constants.clear();
	script->get_constants(&constants);
}

void PlaceHolderScriptInstance::property_set_fallback(const StringName &p_name, const Variant &p_value, bool *r_valid) {
	if (script->is_placeholder_fallback_enabled()) {
		HashMap<StringName, Variant>::Iterator E = values.find(p_name);

		if (E) {
			E->value = p_value;
		} else {
			values.insert(p_name, p_value);
		}

		bool found = false;
		for (const PropertyInfo &F : properties) {
			if (F.name == p_name) {
				found = true;
				break;
			}
		}
		if (!found) {
			PropertyHint hint = PROPERTY_HINT_NONE;
			const Object *obj = p_value.get_validated_object();
			if (obj && obj->is_class("Node")) {
				hint = PROPERTY_HINT_NODE_TYPE;
			}
			properties.push_back(PropertyInfo(p_value.get_type(), p_name, hint, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_SCRIPT_VARIABLE));
		}
	}

	if (r_valid) {
		*r_valid = false; // Cannot change the value in either case
	}
}

Variant PlaceHolderScriptInstance::property_get_fallback(const StringName &p_name, bool *r_valid) {
	if (script->is_placeholder_fallback_enabled()) {
		HashMap<StringName, Variant>::ConstIterator E = values.find(p_name);

		if (E) {
			if (r_valid) {
				*r_valid = true;
			}
			return E->value;
		}

		E = constants.find(p_name);
		if (E) {
			if (r_valid) {
				*r_valid = true;
			}
			return E->value;
		}
	}

	if (r_valid) {
		*r_valid = false;
	}

	return Variant();
}

PlaceHolderScriptInstance::PlaceHolderScriptInstance(ScriptLanguage *p_language, Ref<Script> p_script, Object *p_owner) :
		owner(p_owner),
		language(p_language),
		script(p_script) {
}

PlaceHolderScriptInstance::~PlaceHolderScriptInstance() {
	if (script.is_valid()) {
		script->_placeholder_erased(this);
	}
}
