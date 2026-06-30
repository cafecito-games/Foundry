/**************************************************************************/
/*  fs_cache.cpp                                                          */
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

#include "fs_cache.h"

#include "foundry_script.h"
#include "fs_analyzer.h"
#include "fs_compiler.h"
#include "fs_parser.h"

#include "core/io/file_access.h"
#include "core/templates/vector.h"

FSParserRef::Status FSParserRef::get_status() const {
	return status;
}

String FSParserRef::get_path() const {
	return path;
}

uint32_t FSParserRef::get_source_hash() const {
	return source_hash;
}

FSParser *FSParserRef::get_parser() {
	if (parser == nullptr) {
		parser = memnew(FSParser);
	}
	return parser;
}

FSAnalyzer *FSParserRef::get_analyzer() {
	if (analyzer == nullptr) {
		analyzer = memnew(FSAnalyzer(get_parser()));
	}
	return analyzer;
}

Error FSParserRef::raise_status(Status p_new_status) {
	ERR_FAIL_COND_V(clearing, ERR_BUG);
	ERR_FAIL_COND_V(parser == nullptr && status != EMPTY, ERR_BUG);

	while (result == OK && p_new_status > status) {
		switch (status) {
			case EMPTY: {
				// Calling parse will clear the parser, which can destruct another FSParserRef which can clear the last reference to the script with this path, calling remove_script, which clears this FSParserRef.
				// It's ok if its the first thing done here.
				get_parser()->clear();
				status = PARSED;
				String remapped_path = ResourceLoader::path_remap(path);
				if (remapped_path.has_extension("fsc")) {
					Vector<uint8_t> tokens = FSCache::get_binary_tokens(remapped_path);
					source_hash = hash_djb2_buffer(tokens.ptr(), tokens.size());
					result = get_parser()->parse_binary(tokens, path);
				} else {
					String source = FSCache::get_source_code(remapped_path);
					source_hash = source.hash();
					result = get_parser()->parse(source, path, false);
				}
				if (result == OK) {
					FSCache::update_parser_dependencies(path, get_parser());
				}
			} break;
			case PARSED: {
				status = INHERITANCE_SOLVED;
				result = get_analyzer()->resolve_inheritance();
			} break;
			case INHERITANCE_SOLVED: {
				status = INTERFACE_SOLVED;
				result = get_analyzer()->resolve_interface();
			} break;
			case INTERFACE_SOLVED: {
				status = FULLY_SOLVED;
				result = get_analyzer()->resolve_body();
			} break;
			case FULLY_SOLVED: {
				return result;
			}
		}
	}

	return result;
}

void FSParserRef::clear() {
	if (clearing) {
		return;
	}
	clearing = true;

	FSParser *lparser = parser;
	FSAnalyzer *lanalyzer = analyzer;

	parser = nullptr;
	analyzer = nullptr;
	status = EMPTY;
	result = OK;
	source_hash = 0;

	clearing = false;

	if (lanalyzer != nullptr) {
		memdelete(lanalyzer);
	}

	if (lparser != nullptr) {
		memdelete(lparser);
	}
}

FSParserRef::~FSParserRef() {
	clear();

	if (!abandoned) {
		MutexLock lock(FSCache::singleton->mutex);
		FSCache::singleton->parser_map.erase(path);
	}
}

FSCache *FSCache::singleton = nullptr;

SafeBinaryMutex<FSCache::BINARY_MUTEX_TAG> &_get_fs_cache_mutex() {
	return FSCache::mutex;
}

template <>
thread_local SafeBinaryMutex<FSCache::BINARY_MUTEX_TAG>::TLSData SafeBinaryMutex<FSCache::BINARY_MUTEX_TAG>::tls_data(_get_fs_cache_mutex());
SafeBinaryMutex<FSCache::BINARY_MUTEX_TAG> FSCache::mutex;

void FSCache::move_script(const String &p_from, const String &p_to) {
	if (singleton == nullptr || p_from == p_to || p_from.is_empty()) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

	remove_parser(p_from);

	if (singleton->shallow_fs_cache.has(p_from) && !p_from.is_empty()) {
		singleton->shallow_fs_cache[p_to] = singleton->shallow_fs_cache[p_from];
	}
	singleton->shallow_fs_cache.erase(p_from);

	if (singleton->full_fs_cache.has(p_from) && !p_from.is_empty()) {
		singleton->full_fs_cache[p_to] = singleton->full_fs_cache[p_from];
	}
	singleton->full_fs_cache.erase(p_from);
}

void FSCache::remove_script(const String &p_path) {
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

	if (HashMap<String, Vector<ObjectID>>::Iterator E = singleton->abandoned_parser_map.find(p_path)) {
		for (ObjectID parser_ref_id : E->value) {
			Ref<FSParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.erase(p_path);

	if (singleton->parser_map.has(p_path)) {
		singleton->parser_map[p_path]->clear();
	}

	remove_parser(p_path);

	singleton->dependencies.erase(p_path);
	singleton->shallow_fs_cache.erase(p_path);
	singleton->full_fs_cache.erase(p_path);
}

Ref<FSParserRef> FSCache::get_parser(const String &p_path, FSParserRef::Status p_status, Error &r_error, const String &p_owner) {
	MutexLock lock(singleton->mutex);
	Ref<FSParserRef> ref;
	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
		singleton->parser_inverse_dependencies[p_path].insert(p_owner);
	}
	if (singleton->parser_map.has(p_path)) {
		ref = Ref<FSParserRef>(singleton->parser_map[p_path]);
		if (ref.is_null()) {
			r_error = ERR_INVALID_DATA;
			return ref;
		}
	} else {
		String remapped_path = ResourceLoader::path_remap(p_path);
		if (!FileAccess::exists(remapped_path)) {
			r_error = ERR_FILE_NOT_FOUND;
			return ref;
		}
		ref.instantiate();
		ref->path = p_path;
		singleton->parser_map[p_path] = ref.ptr();
	}
	r_error = ref->raise_status(p_status);

	return ref;
}

bool FSCache::has_parser(const String &p_path) {
	MutexLock lock(singleton->mutex);
	return singleton->parser_map.has(p_path);
}

void FSCache::clear_parser_dependency_edges(const String &p_path) {
	if (singleton == nullptr) {
		return;
	}

	if (HashMap<String, HashSet<String>>::Iterator forward = singleton->parser_dependencies.find(p_path)) {
		for (const String &dep : forward->value) {
			if (HashMap<String, HashSet<String>>::Iterator inverse = singleton->parser_inverse_dependencies.find(dep)) {
				inverse->value.erase(p_path);
				if (inverse->value.is_empty()) {
					singleton->parser_inverse_dependencies.erase(dep);
				}
			}
		}
		singleton->parser_dependencies.erase(p_path);
	}
}

void FSCache::update_parser_dependencies(const String &p_path, const FSParser *p_parser) {
	if (singleton == nullptr || p_parser == nullptr) {
		return;
	}

	clear_parser_dependency_edges(p_path);

	HashSet<String> deps;
	for (const String &dep : p_parser->get_dependencies()) {
		deps.insert(dep);
		singleton->parser_inverse_dependencies[dep].insert(p_path);
	}
	if (!deps.is_empty()) {
		singleton->parser_dependencies[p_path] = deps;
	}
}

void FSCache::remove_parser(const String &p_path) {
	MutexLock lock(singleton->mutex);

	clear_parser_dependency_edges(p_path);

	if (singleton->parser_map.has(p_path)) {
		FSParserRef *parser_ref = singleton->parser_map[p_path];
		parser_ref->abandoned = true;
		singleton->abandoned_parser_map[p_path].push_back(parser_ref->get_instance_id());
	}

	// Can't clear the parser because some other parser might be currently using it in the chain of calls.
	singleton->parser_map.erase(p_path);

	// Have to copy while iterating, because parser_inverse_dependencies is modified.
	HashSet<String> ideps = singleton->parser_inverse_dependencies[p_path];
	singleton->parser_inverse_dependencies.erase(p_path);
	for (String idep_path : ideps) {
		remove_parser(idep_path);
	}
}

String FSCache::get_source_code(const String &p_path) {
	if (singleton != nullptr) {
		MutexLock lock(singleton->mutex);
		if (HashMap<String, String>::ConstIterator override = singleton->source_overrides.find(p_path)) {
			return override->value;
		}
	}

	Vector<uint8_t> source_file;
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V(err, "");

	uint64_t len = f->get_length();
	source_file.resize(len + 1);
	uint64_t r = f->get_buffer(source_file.ptrw(), len);
	ERR_FAIL_COND_V(r != len, "");
	source_file.write[len] = 0;

	String source;
	if (source.append_utf8((const char *)source_file.ptr(), len) != OK) {
		ERR_FAIL_V_MSG("", "Script '" + p_path + "' contains invalid unicode (UTF-8), so it was not loaded. Please ensure that scripts are saved in valid UTF-8 unicode.");
	}
	return source;
}

void FSCache::set_source_override(const String &p_path, const String &p_source) {
	if (singleton == nullptr) {
		return;
	}
	MutexLock lock(singleton->mutex);
	singleton->source_overrides[p_path] = p_source;
}

bool FSCache::has_source_override(const String &p_path) {
	if (singleton == nullptr) {
		return false;
	}
	MutexLock lock(singleton->mutex);
	return singleton->source_overrides.has(p_path);
}

void FSCache::clear_source_override(const String &p_path) {
	if (singleton == nullptr) {
		return;
	}
	MutexLock lock(singleton->mutex);
	singleton->source_overrides.erase(p_path);
}

void FSCache::clear_source_overrides() {
	if (singleton == nullptr) {
		return;
	}
	MutexLock lock(singleton->mutex);
	singleton->source_overrides.clear();
}

HashSet<String> FSCache::get_inverse_dependencies(const String &p_path) {
	if (singleton == nullptr) {
		return HashSet<String>();
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return HashSet<String>();
	}

	if (singleton->parser_inverse_dependencies.has(p_path)) {
		return singleton->parser_inverse_dependencies[p_path];
	}
	return HashSet<String>();
}

Vector<uint8_t> FSCache::get_binary_tokens(const String &p_path) {
	Vector<uint8_t> buffer;
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(err != OK, buffer, "Failed to open binary FoundryScript file '" + p_path + "'.");

	uint64_t len = f->get_length();
	buffer.resize(len);
	uint64_t read = f->get_buffer(buffer.ptrw(), buffer.size());
	ERR_FAIL_COND_V_MSG(read != len, Vector<uint8_t>(), "Failed to read binary FoundryScript file '" + p_path + "'.");

	return buffer;
}

Ref<FoundryScript> FSCache::get_shallow_script(const String &p_path, Error &r_error, const String &p_owner) {
	MutexLock lock(singleton->mutex);

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}
	if (singleton->full_fs_cache.has(p_path)) {
		return singleton->full_fs_cache[p_path];
	}
	if (singleton->shallow_fs_cache.has(p_path)) {
		return singleton->shallow_fs_cache[p_path];
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	Ref<FoundryScript> script;
	script.instantiate();

	script->set_path_cache(p_path);
	if (remapped_path.has_extension("fsc")) {
		Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
		if (buffer.is_empty()) {
			r_error = ERR_FILE_CANT_READ;
		}
		script->set_binary_tokens_source(buffer);
	} else if (singleton->source_overrides.has(remapped_path)) {
		script->set_source_code(singleton->source_overrides[remapped_path]);
		script->set_path_cache(p_path);
	} else {
		r_error = script->load_source_code(remapped_path);
	}

	if (r_error) {
		return Ref<FoundryScript>(); // Returns null and does not cache when the script fails to load.
	}

	Ref<FSParserRef> parser_ref = get_parser(p_path, FSParserRef::PARSED, r_error);
	if (r_error == OK) {
		FSCompiler::make_scripts(script.ptr(), parser_ref->get_parser()->get_tree(), true);
	}

	singleton->shallow_fs_cache[p_path] = script;

	return script;
}

Ref<FoundryScript> FSCache::get_full_script(const String &p_path, Error &r_error, const String &p_owner, bool p_update_from_disk) {
	MutexLock lock(singleton->mutex);

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}

	Ref<FoundryScript> script;
	r_error = OK;
	if (singleton->full_fs_cache.has(p_path)) {
		script = singleton->full_fs_cache[p_path];
		if (!p_update_from_disk) {
			return script;
		}
	}

	if (script.is_null()) {
		script = get_shallow_script(p_path, r_error);
		// Only exit early if script failed to load, otherwise let reload report errors.
		if (script.is_null()) {
			return script;
		}
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	if (p_update_from_disk) {
		if (remapped_path.has_extension("fsc")) {
			Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
			if (buffer.is_empty()) {
				r_error = ERR_FILE_CANT_READ;
				goto finish;
			}
			script->set_binary_tokens_source(buffer);
		} else if (singleton->source_overrides.has(remapped_path)) {
			script->set_source_code(singleton->source_overrides[remapped_path]);
		} else {
			r_error = script->load_source_code(remapped_path);
			if (r_error) {
				goto finish;
			}
		}
	}

	// Allowing lifting the lock might cause a script to be reloaded multiple times,
	// which, as a last resort deadlock prevention strategy, is a good tradeoff.
	{
		uint32_t allowance_id = WorkerThreadPool::thread_enter_unlock_allowance_zone(singleton->mutex);
		r_error = script->reload(true);
		WorkerThreadPool::thread_exit_unlock_allowance_zone(allowance_id);
	}

finish:
	singleton->full_fs_cache[p_path] = script;
	singleton->shallow_fs_cache.erase(p_path);

	// Add the script to the resource cache. Usually ResourceLoader would take care of it, but cyclic references can break that sometimes so we do it ourselves.
	// Resources don't know whether they are cached, so using `set_path()` after `set_path_cache()` does not add the resource to the cache if the path is the same.
	// We reset the cached path from `get_shallow_script()` so that the subsequent call to `set_path()` caches everything correctly.
	script->set_path_cache(String());
	script->set_path(p_path, true);

	return script;
}

Ref<FoundryScript> FSCache::get_cached_script(const String &p_path) {
	MutexLock lock(singleton->mutex);

	if (singleton->full_fs_cache.has(p_path)) {
		return singleton->full_fs_cache[p_path];
	}

	if (singleton->shallow_fs_cache.has(p_path)) {
		return singleton->shallow_fs_cache[p_path];
	}

	return Ref<FoundryScript>();
}

Error FSCache::finish_compiling(const String &p_owner) {
	MutexLock lock(singleton->mutex);

	// Mark this as compiled.
	Ref<FoundryScript> script = get_cached_script(p_owner);
	singleton->full_fs_cache[p_owner] = script;
	singleton->shallow_fs_cache.erase(p_owner);

	HashSet<String> depends = singleton->dependencies[p_owner];

	Error err = OK;
	for (const String &E : depends) {
		Error this_err = OK;
		// No need to save the script. We assume it's already referenced in the owner.
		get_full_script(E, this_err);

		if (this_err != OK) {
			err = this_err;
		}
	}

	singleton->dependencies.erase(p_owner);

	return err;
}

void FSCache::add_static_script(Ref<FoundryScript> p_script) {
	ERR_FAIL_COND_MSG(p_script.is_null(), "Trying to cache empty script as static.");
	ERR_FAIL_COND_MSG(!p_script->is_valid(), "Trying to cache non-compiled script as static.");
	singleton->static_fs_cache[p_script->get_fully_qualified_name()] = p_script;
}

void FSCache::remove_static_script(const String &p_fqcn) {
	singleton->static_fs_cache.erase(p_fqcn);
}

void FSCache::invalidate_analysis() {
	if (singleton == nullptr) {
		return;
	}

	Vector<String> parser_paths;
	{
		MutexLock lock(singleton->mutex);
		parser_paths.resize(singleton->parser_map.size());
		int index = 0;
		for (const KeyValue<String, FSParserRef *> &E : singleton->parser_map) {
			parser_paths.write[index++] = E.key;
		}
	}

	// remove_parser() abandons the entry and recurses into inverse dependencies, so it must run
	// without the lock held (it re-acquires it) and tolerates paths already removed by a prior
	// recursive call.
	for (const String &path : parser_paths) {
		remove_parser(path);
	}

	MutexLock lock(singleton->mutex);
	// Drop the analyzed-script artifacts so a subsequent load rebuilds them under the new settings.
	// Source overrides are intentionally preserved: an in-progress edit's buffer must outlive a
	// settings flip so the re-analysis still sees the unsaved source.
	singleton->shallow_fs_cache.clear();
	singleton->full_fs_cache.clear();
	singleton->static_fs_cache.clear();
}

void FSCache::clear() {
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}
	singleton->cleared = true;

	singleton->parser_dependencies.clear();
	singleton->parser_inverse_dependencies.clear();

	for (const KeyValue<String, Vector<ObjectID>> &KV : singleton->abandoned_parser_map) {
		for (ObjectID parser_ref_id : KV.value) {
			Ref<FSParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.clear();

	RBSet<Ref<FSParserRef>> parser_map_refs;
	for (KeyValue<String, FSParserRef *> &E : singleton->parser_map) {
		parser_map_refs.insert(E.value);
	}

	singleton->parser_map.clear();

	for (Ref<FSParserRef> &E : parser_map_refs) {
		if (E.is_valid()) {
			E->clear();
		}
	}

	parser_map_refs.clear();
	singleton->source_overrides.clear();
	singleton->shallow_fs_cache.clear();
	singleton->full_fs_cache.clear();
	singleton->static_fs_cache.clear();

	// `cleared` only guards against re-entrant `remove_script()`/`move_script()`
	// calls triggered while the caches above are being emptied. Once that is done
	// the cache is reusable, so reset the flag to allow a later `clear()` (e.g.
	// when the language is re-initialized) to run instead of becoming a no-op.
	singleton->cleared = false;
}

FSCache::FSCache() {
	singleton = this;
}

FSCache::~FSCache() {
	if (!cleared) {
		clear();
	}
	singleton = nullptr;
}
