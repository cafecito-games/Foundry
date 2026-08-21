/**************************************************************************/
/*  fs_temporary_project_tree.h                                           */
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

#include "tests/test_macros.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/vector.h"

#ifdef UNIX_ENABLED
#include <sys/types.h>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#endif // UNIX_ENABLED

#ifdef WINDOWS_ENABLED
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // WINDOWS_ENABLED

namespace FSTests {

// Builds a throwaway project tree on disk under the shared test scratch path and removes it on
// destruction, so filesystem-walking code runs against real directories without touching the test
// project or res://.
//
// The helper owns exactly one validated subtree and is unable to create, write, or delete anywhere
// else:
//
//   - the scratch root is resolved once, must be absolute, and is canonicalized so aliases and
//     symlinks cannot defeat containment checks;
//   - a child name must be relative and free of `..`, absolute prefixes, drive/share prefixes, and
//     schemes;
//   - creation, writing, and deletion all require the target to be a strict descendant of the
//     scratch root, compared component by component (so `/tmp/a` does not contain `/tmp/ab`);
//   - cleanup never traverses a symlink, including when the cleanup root is itself a symlink;
//   - any path that contains the running executable is refused as a second, independent check;
//   - a failed validation returns an `Error` and performs no deletion, directory creation, or file
//     write.
struct TemporaryProjectTree {
	String root;

	// Reports a setup failure of the owned tree. Inside a doctest case it fails that case; the
	// type-completeness command-line entry point drives these same helpers with no doctest context at
	// all, where the assertion machinery is unusable, so there the failure is printed instead.
	static void report_failure(bool p_ok, const String &p_message) {
		if (p_ok) {
			return;
		}
		if (doctest::is_running_in_test) {
			CHECK_MESSAGE(p_ok, p_message);
			return;
		}
		ERR_PRINT(p_message);
	}

	explicit TemporaryProjectTree(const String &p_name) {
		// The root is captured once here so cleanup is validated against the very root this tree was
		// created under, whatever the environment looks like at destruction time.
		owned_scratch_root = get_test_scratch_root();
		setup_error = owned_scratch_root.is_empty() ? ERR_UNCONFIGURED : resolve_owned_child(owned_scratch_root, p_name, root);
		report_failure(setup_error == OK, vformat("Cannot resolve an owned scratch path for '%s'", p_name));
		if (setup_error != OK) {
			root = String();
			return;
		}

		// Start from a clean slate in case a previous aborted run left the tree behind.
		setup_error = remove_validated_descendant(owned_scratch_root, root);
		report_failure(setup_error == OK, vformat("Cannot clear scratch path '%s'", root));
		if (setup_error != OK) {
			root = String();
			return;
		}

		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (dir.is_null()) {
			setup_error = ERR_CANT_CREATE;
			report_failure(false, "Cannot access the filesystem to create a scratch project tree");
			root = String();
			return;
		}
		setup_error = dir->make_dir_recursive(root);
		report_failure(setup_error == OK, vformat("Cannot create the scratch project tree '%s'", root));
		if (setup_error != OK) {
			root = String();
		}
	}

	~TemporaryProjectTree() {
		if (root.is_empty()) {
			return;
		}
		remove_validated_descendant(owned_scratch_root, root);
	}

	Error get_setup_error() const {
		return setup_error;
	}

	bool is_valid() const {
		return setup_error == OK && !root.is_empty();
	}

	// Writes p_contents to root/p_relative_path, creating intermediate directories as needed. A
	// relative path that would escape the owned tree is refused before anything is created.
	void write_file(const String &p_relative_path, const String &p_contents) const {
		String absolute_path;
		const Error path_error = resolve_owned_child(root, p_relative_path, absolute_path);
		report_failure(path_error == OK, vformat("Refusing to write '%s' outside the owned scratch tree", p_relative_path));
		if (path_error != OK) {
			return;
		}

		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (dir.is_null()) {
			report_failure(false, "Cannot access the filesystem to write a scratch file");
			return;
		}
		const Error directory_error = dir->make_dir_recursive(absolute_path.get_base_dir());
		report_failure(directory_error == OK,
				vformat("Cannot create the directory of '%s'", absolute_path));
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		report_failure(file.is_valid(), vformat("Cannot write '%s'", absolute_path));
		if (file.is_null()) {
			return;
		}
		file->store_string(p_contents);
	}

	// The absolute, canonical scratch root every owned path must live under. Empty when the
	// configured root is unusable, in which case no owned path can be produced at all.
	static String get_test_scratch_root() {
		// The configured value is re-validated and re-canonicalized on every resolution rather than
		// memoized, so a test that scopes `FOUNDRY_TEST_SCRATCH` still redirects staging. That does
		// not reintroduce working-directory sensitivity: a relative value is always rejected, and an
		// absolute one canonicalizes to the same path from any working directory. Each owned path is
		// resolved from the root once and retains it, so an object always validates its own cleanup
		// against the root it was created under.
		if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
			const String configured_root = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
			if (!configured_root.is_empty()) {
				String absolute_root;
				if (resolve_scratch_root(configured_root, absolute_root) != OK) {
					ERR_PRINT(vformat("FOUNDRY_TEST_SCRATCH '%s' is not a usable absolute scratch root; test scratch is unavailable.", configured_root));
					return String();
				}
				return absolute_root;
			}
		}
		// Without an explicit `FOUNDRY_TEST_SCRATCH`, fall back to a directory scoped to this
		// process. A fixed shared path would let two `foundry` test processes running
		// concurrently on the same machine (e.g. separate worktrees during a multi-agent
		// session) race on the same staged project tree: one process's cleanup + `copy_dir`
		// in `stage_project_copy` can interleave with another's, corrupting the staged files
		// each currently-running test depends on.
		static const String process_scoped_root = []() -> String {
			String base;
			if (resolve_scratch_root(OS::get_singleton()->get_temp_path(), base) != OK) {
				ERR_PRINT("Cannot resolve an absolute OS temporary directory; test scratch is unavailable.");
				return String();
			}
			// Sweep scratch directories left behind by processes that are no longer running
			// (crashed, killed, or otherwise never reached their own cleanup) so direct,
			// `FOUNDRY_TEST_SCRATCH`-less invocations don't accumulate one staged project copy
			// per past run.
			reap_dead_process_scratch_dirs(base);
			return base.path_join(vformat("foundry-tests-%d", OS::get_singleton()->get_process_id()));
		}();
		return process_scoped_root;
	}

	// Absolute path of an owned scratch child, or an empty String when p_name is not a safe
	// relative name or the scratch root is unusable. Produces no filesystem side effects.
	static String get_test_scratch_path(const String &p_name) {
		String absolute_path;
		if (resolve_owned_path(p_name, absolute_path) != OK) {
			return String();
		}
		return absolute_path;
	}

	// Validation-only resolution of an owned scratch child. Never touches the filesystem, so it is
	// also the safe way to prove a rejection happened before any mutation.
	static Error resolve_owned_path(const String &p_name, String &r_absolute_path) {
		r_absolute_path = String();
		const String scratch_root = get_test_scratch_root();
		if (scratch_root.is_empty()) {
			return ERR_UNCONFIGURED;
		}
		return resolve_owned_child(scratch_root, p_name, r_absolute_path);
	}

	// Canonicalizes an existing absolute file or directory and proves that its resolved target is
	// owned by the configured test scratch. Unlike resolve_owned_path(), this follows filesystem
	// aliases and therefore detects a lexically contained path that resolves outside scratch.
	static Error resolve_existing_owned_path(const String &p_path, String &r_canonical_path) {
		r_canonical_path = String();
		if (!is_absolute_filesystem_path(p_path)) {
			return ERR_CANT_RESOLVE;
		}
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_null() ||
				(!filesystem->dir_exists(p_path) && !filesystem->file_exists(p_path))) {
			return ERR_CANT_RESOLVE;
		}
		const String scratch_root = get_test_scratch_root();
		if (scratch_root.is_empty()) {
			return ERR_CANT_RESOLVE;
		}
		const String canonical_scratch_root = canonicalize_existing_path(scratch_root);
		const String canonical_path = canonicalize_existing_path(p_path);
		if (!is_absolute_filesystem_path(canonical_scratch_root) ||
				!is_absolute_filesystem_path(canonical_path)) {
			return ERR_CANT_RESOLVE;
		}
		if (!is_strict_descendant(canonical_scratch_root, canonical_path)) {
			return ERR_UNAUTHORIZED;
		}
		r_canonical_path = canonical_path;
		return OK;
	}

	// Recursively removes an owned scratch path. Refuses, without deleting anything, when the path
	// is not a strict descendant of the scratch root or when it contains the running executable.
	static Error remove_owned_path(const String &p_absolute_path) {
		const String scratch_root = get_test_scratch_root();
		if (scratch_root.is_empty()) {
			return ERR_UNCONFIGURED;
		}
		return remove_validated_descendant(scratch_root, p_absolute_path);
	}

	// The only containment-checked recursive removal available to callers: p_absolute_path is
	// removed exactly when it is a strict descendant of p_container_root and does not contain the
	// running executable. Both refusals return an `Error` and delete nothing.
	static Error remove_validated_descendant(const String &p_container_root, const String &p_absolute_path) {
		if (path_contains_running_executable(p_absolute_path)) {
			ERR_PRINT(vformat("Refusing to recursively delete '%s' because it contains the running executable '%s'. Check FOUNDRY_TEST_SCRATCH.", p_absolute_path, OS::get_singleton()->get_executable_path()));
			return ERR_UNAUTHORIZED;
		}
		if (!is_strict_descendant(p_container_root, p_absolute_path)) {
			ERR_PRINT(vformat("Refusing to recursively delete '%s' because it is not inside the owned tree '%s'.", p_absolute_path, p_container_root));
			return ERR_UNAUTHORIZED;
		}
		return remove_recursive(p_absolute_path);
	}

	// Resolves a configured scratch root to an absolute canonical filesystem path, creating the
	// directory when it does not exist yet. A relative, scheme-qualified, or otherwise unusable
	// value is rejected before anything is created.
	static Error resolve_scratch_root(const String &p_configured_root, String &r_absolute_root) {
		r_absolute_root = String();
		if (!is_absolute_filesystem_path(p_configured_root)) {
			return ERR_INVALID_PARAMETER;
		}

		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_null()) {
			return ERR_CANT_CREATE;
		}
		if (!filesystem->dir_exists(p_configured_root)) {
			const Error make_error = filesystem->make_dir_recursive(p_configured_root);
			if (make_error != OK) {
				return make_error;
			}
		}

		// Canonicalizing an existing directory is what makes later containment checks meaningful:
		// an alias or symlinked scratch root would otherwise compare unequal to the real paths the
		// tests write and delete.
		const String canonical_root = canonicalize_existing_path(p_configured_root);
		if (!is_absolute_filesystem_path(canonical_root)) {
			return ERR_CANT_RESOLVE;
		}
		r_absolute_root = canonical_root;
		return OK;
	}

	// True when p_path is p_ancestor's strict descendant, compared component by component so a
	// shared string prefix (`/tmp/a` against `/tmp/ab`) never counts as containment. Equality is
	// deliberately not containment.
	static bool is_strict_descendant(const String &p_ancestor, const String &p_path) {
		if (!is_absolute_filesystem_path(p_ancestor) || !is_absolute_filesystem_path(p_path)) {
			return false;
		}
		const Vector<String> ancestor_components = get_path_components(p_ancestor);
		const Vector<String> path_components = get_path_components(p_path);
		if (ancestor_components.is_empty() || path_components.size() <= ancestor_components.size()) {
			return false;
		}
		for (int index = 0; index < ancestor_components.size(); index++) {
			if (ancestor_components[index] != path_components[index]) {
				return false;
			}
		}
		return true;
	}

	// True when p_path is the running executable or one of its ancestor directories. Kept as an
	// independent refusal on top of scratch containment: a scratch root misconfigured onto the
	// binary's directory must never be deletable, whatever the containment check concludes.
	static bool path_contains_running_executable(const String &p_path) {
		const String executable_path = OS::get_singleton()->get_executable_path();
		if (executable_path.is_empty() || p_path.is_empty()) {
			return false;
		}
		const Vector<String> path_components = get_path_components(p_path);
		const Vector<String> executable_components = get_path_components(executable_path);
		if (path_components.is_empty() || executable_components.size() < path_components.size()) {
			return false;
		}
		for (int index = 0; index < path_components.size(); index++) {
			if (path_components[index] != executable_components[index]) {
				return false;
			}
		}
		return true;
	}

	static String stage_project_copy(const String &p_source_root, const String &p_name) {
		Error err = OK;
		Ref<DirAccess> source_dir = DirAccess::open(p_source_root, &err);
		report_failure(err == OK, vformat("Cannot open source project root '%s'", p_source_root));
		if (source_dir.is_null()) {
			return String();
		}
		source_dir->set_include_hidden(true);

		const String source_root = source_dir->get_current_dir().simplify_path();
		const String staged_root = get_test_scratch_path(p_name);
		report_failure(!staged_root.is_empty(), vformat("Cannot resolve an owned scratch path for '%s'", p_name));
		if (staged_root.is_empty()) {
			return String();
		}

		static String last_source_root;
		static String last_staged_root;
		if (last_source_root != source_root || last_staged_root != staged_root ||
				!FileAccess::exists(staged_root.path_join("project.foundry"))) {
			const Error remove_error = remove_owned_path(staged_root);
			report_failure(remove_error == OK, vformat("Cannot clear staged project '%s'", staged_root));
			if (remove_error != OK) {
				return String();
			}
			const Error copy_err = source_dir->copy_dir(source_root, staged_root);
			report_failure(copy_err == OK, vformat("Cannot stage test project '%s' at '%s'", source_root, staged_root));
			if (copy_err != OK) {
				return String();
			}
			last_source_root = source_root;
			last_staged_root = staged_root;
		}

		return staged_root;
	}

	// Absolute filesystem path of an existing file or directory with symlinks and aliases resolved.
	// Empty when the path does not exist or cannot be resolved.
	static String canonicalize_existing_path(const String &p_path) {
#ifdef UNIX_ENABLED
		char *resolved = ::realpath(p_path.utf8().get_data(), nullptr);
		if (resolved == nullptr) {
			return String();
		}
		String canonical;
		const Error parse_error = canonical.append_utf8(resolved);
		::free(resolved);
		if (parse_error != OK) {
			return String();
		}
		return canonical.simplify_path();
#elif defined(WINDOWS_ENABLED)
		HANDLE handle = ::CreateFileW((LPCWSTR)(p_path.utf16().get_data()), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			return String();
		}
		WCHAR buffer[4096];
		const DWORD length = ::GetFinalPathNameByHandleW(handle, buffer, 4095, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		::CloseHandle(handle);
		if (length == 0 || length > 4095) {
			return String();
		}
		buffer[length] = 0;
		const String canonical = String::utf16((const char16_t *)buffer, (int)length);
		return canonical.trim_prefix("\\\\?\\").replace("\\", "/").simplify_path();
#else
		return p_path.simplify_path();
#endif
	}

private:
	String owned_scratch_root;
	Error setup_error = OK;

	// Splits a path into comparison-ready components. Windows paths are compared case-insensitively
	// and with normalized separators; every platform drops empty segments and lexical `.`/`..`.
	static Vector<String> get_path_components(const String &p_path) {
		Vector<String> components = p_path.replace("\\", "/").simplify_path().split("/", false);
#ifdef WINDOWS_ENABLED
		for (int index = 0; index < components.size(); index++) {
			components.write[index] = components[index].to_lower();
		}
#endif // WINDOWS_ENABLED
		return components;
	}

	// True for a path rooted at a filesystem root or drive. A scheme-qualified path (`res://`,
	// `user://`) is not a filesystem path and is rejected.
	static bool is_absolute_filesystem_path(const String &p_path) {
		if (p_path.is_empty() || p_path.contains("://")) {
			return false;
		}
		return p_path.is_absolute_path();
	}

	// True only for a relative path that cannot escape its container: no absolute or network-share
	// prefix, no drive letter or scheme (both of which contain `:`), and no `.`/`..` component.
	static bool is_safe_relative_path(const String &p_relative_path) {
		if (p_relative_path.is_empty() || p_relative_path.contains(":")) {
			return false;
		}
		if (p_relative_path.is_absolute_path() || p_relative_path.is_network_share_path()) {
			return false;
		}
		const Vector<String> components = p_relative_path.replace("\\", "/").split("/", false);
		if (components.is_empty()) {
			return false;
		}
		for (const String &component : components) {
			if (component == "." || component == "..") {
				return false;
			}
		}
		return true;
	}

	static Error resolve_owned_child(const String &p_container, const String &p_relative_path, String &r_absolute_path) {
		r_absolute_path = String();
		if (!is_absolute_filesystem_path(p_container)) {
			return ERR_UNCONFIGURED;
		}
		if (!is_safe_relative_path(p_relative_path)) {
			return ERR_INVALID_PARAMETER;
		}
		const String candidate = p_container.path_join(p_relative_path).simplify_path();
		if (!is_strict_descendant(p_container, candidate)) {
			return ERR_UNAUTHORIZED;
		}
		r_absolute_path = candidate;
		return OK;
	}

	// Removes a tree whose containment has already been validated by the caller. Private on purpose:
	// arbitrary recursive deletion must not be reachable without validation.
	static Error remove_recursive(const String &p_path) {
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_null()) {
			return ERR_CANT_CREATE;
		}

		// A link is removable only as a leaf, and only now that its parent passed validation. This
		// includes the cleanup root itself: opening it would walk into the link's target, which
		// lives outside the validated subtree.
		if (filesystem->is_link(p_path)) {
			return DirAccess::remove_absolute(p_path);
		}
		if (!filesystem->dir_exists(p_path)) {
			if (filesystem->file_exists(p_path)) {
				return DirAccess::remove_absolute(p_path);
			}
			return OK;
		}

		Ref<DirAccess> dir = DirAccess::open(p_path);
		if (dir.is_null()) {
			return ERR_CANT_OPEN;
		}
		dir->set_include_hidden(true);

		// The listing is collected before anything is removed so the directory is not mutated while
		// it is being enumerated.
		Vector<String> child_directories;
		Vector<String> leaves;
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(child)) {
				child_directories.push_back(child);
			} else {
				leaves.push_back(child);
			}
		}
		dir->list_dir_end();

		Error result = OK;
		for (const String &leaf : leaves) {
			const Error remove_error = DirAccess::remove_absolute(leaf);
			if (remove_error != OK) {
				result = remove_error;
			}
		}
		for (const String &child : child_directories) {
			const Error remove_error = remove_recursive(child);
			if (remove_error != OK) {
				result = remove_error;
			}
		}
		const Error remove_error = DirAccess::remove_absolute(p_path);
		if (remove_error != OK) {
			result = remove_error;
		}
		return result;
	}

	// A grace period before a `foundry-tests-<pid>` directory is even considered for
	// reaping. This alone cannot prove the owning process exited (a paused debugger
	// session, or a backward wall-clock jump, could make a live process's directory look
	// old), so it is only ever used to gate `process_is_definitely_dead()` below, never as
	// the sole reason to delete.
	static constexpr uint64_t STALE_SCRATCH_AGE_SECONDS = 12 * 60 * 60;

	// True only when the platform can prove `p_pid` no longer names a running process, for
	// any process on the system, not just this one's own children. `OS::is_process_running()`
	// cannot be reused here: on Unix it is implemented via `waitpid` and on Windows it looks
	// up the engine's own child-process table, so both only answer for processes this
	// engine instance itself started. A live but unrelated sibling `foundry` test process
	// must never be mistaken for dead, so every ambiguous outcome (permission denied, an
	// unsupported platform) is treated as "still alive".
	static bool process_is_definitely_dead(int64_t p_pid) {
#ifdef UNIX_ENABLED
		// `kill(pid, 0)` sends no signal; it only probes whether `pid` exists and is
		// visible to this user. `ESRCH` is the only outcome that proves the process is
		// gone; `EPERM` means it exists but is owned by someone else.
		return ::kill((pid_t)p_pid, 0) != 0 && errno == ESRCH;
#elif defined(WINDOWS_ENABLED)
		// `OpenProcess` fails for a PID no process on the system currently holds, which is
		// enough on its own to prove `p_pid` is gone (unlike `GetExitCodeProcess`, this
		// does not require having started or otherwise tracked the process).
		HANDLE process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)p_pid);
		if (process_handle == nullptr) {
			return GetLastError() == ERROR_INVALID_PARAMETER;
		}
		CloseHandle(process_handle);
		return false;
#else
		// Without a reliable cross-process liveness check, never claim a directory is
		// safe to delete based on age alone.
		return false;
#endif
	}

	// Removes `foundry-tests-<pid>` directories under `p_base` whose owning process is
	// provably no longer running and that have not been touched recently, so scratch
	// directories from earlier direct test invocations are reclaimed the next time any
	// `foundry` test process starts, without ever touching a live process's staged
	// project.
	static void reap_dead_process_scratch_dirs(const String &p_base) {
		Ref<DirAccess> dir = DirAccess::open(p_base);
		if (dir.is_null()) {
			return;
		}
		const uint64_t now = OS::get_singleton()->get_unix_time();
		dir->list_dir_begin();
		Vector<String> reapable;
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			const String child = p_base.path_join(entry);
			// Never follow a symlink here: a planted (or merely stale, from a different
			// tool) symlink named `foundry-tests-<n>` in the shared system temp directory
			// must not cause a recursive delete outside the scratch area.
			if (!dir->current_is_dir() || dir->is_link(child) || !entry.begins_with("foundry-tests-")) {
				continue;
			}
			const String pid_text = entry.trim_prefix("foundry-tests-");
			if (!pid_text.is_valid_int()) {
				continue;
			}
			const uint64_t modified_time = FileAccess::get_modified_time(child);
			const bool recently_touched = modified_time != 0 && now >= modified_time && now - modified_time < STALE_SCRATCH_AGE_SECONDS;
			if (recently_touched || !process_is_definitely_dead(pid_text.to_int())) {
				continue;
			}
			reapable.push_back(child);
		}
		dir->list_dir_end();

		for (const String &child : reapable) {
			// The reaped directories are siblings of this process's own scratch root, so they are
			// validated against the shared base rather than against the scratch root itself.
			remove_validated_descendant(p_base, child);
		}
	}
};

} // namespace FSTests
