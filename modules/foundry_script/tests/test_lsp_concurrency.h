/**************************************************************************/
/*  test_lsp_concurrency.h                                                */
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

#ifdef TOOLS_ENABLED

#include "tests/test_macros.h"

#include "../foundry_script.h"
#include "../fs_cache.h"

#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"

#include "test_lsp.h" // FSTests::initialize, root, make_did_*_params, TestFSLanguageProtocolInitializer

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace FSTests {

namespace {

// Regression coverage for the thread-safety design in #1422. The shared `clients`/`parse_results`
// maps and the raw `ExtendFSParser *` pointers that escape the protocol are now guarded by one
// recursive protocol mutex, while the main-thread invalidation funnels enqueue into a small
// pending record and never block on that mutex (D3). These cases pin the deferred semantics
// (cases 1 and 3, deterministic) and exercise mutual exclusion under concurrency (case 2).

struct LockHolderData {
	FSLanguageProtocol *protocol = nullptr;
	SafeFlag holding;
};

void hold_protocol_lock_then_sleep(void *p_userdata) {
	set_current_thread_safe_for_nodes(true);
	LockHolderData *data = static_cast<LockHolderData *>(p_userdata);
	// Hold the protocol mutex and signal the main thread, then sleep long enough that an
	// inline-locking funnel would still be blocked when the main thread checks.
	MutexLock lock(data->protocol->mutex);
	data->holding.set();
	OS::get_singleton()->delay_usec(500000); // 500 ms
	data->holding.clear();
}

struct StressWorkerData {
	FSLanguageProtocol *protocol = nullptr;
	String path_a;
	SafeFlag done;
	SafeNumeric<int> null_parser_seen;
};

void stress_worker(void *p_userdata) {
	set_current_thread_safe_for_nodes(true);
	StressWorkerData *data = static_cast<StressWorkerData *>(p_userdata);
	for (int i = 0; i < 200; i++) {
		data->protocol->apply_pending_invalidations();
		// Hold the protocol lock across the dereference (D2): the main thread's didChange cannot
		// memdelete this parser until we release, so the diagnostics read is use-after-free-free.
		MutexLock lock(data->protocol->mutex);
		ExtendFSParser *parser = data->protocol->peek_parse_result(data->path_a);
		if (parser == nullptr) {
			data->null_parser_seen.increment();
			continue;
		}
		(void)parser->get_diagnostics();
	}
	data->done.set();
}

} // namespace

TEST_CASE("[Modules][FoundryScript][LSP] Disk-source invalidation does not block on the protocol lock") {
	EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
	FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
	REQUIRE(protocol);
	TestFSLanguageProtocolInitializer::mark_initialized(protocol);

	const String path = "res://lsp/concurrency_nonblocking.fs";
	{
		ScopedLSPTempFile file(path, "extends RefCounted\nfunc ping() -> void:\n\tpass\n");
	}

	LockHolderData data;
	data.protocol = protocol;

	Thread worker;
	worker.start(hold_protocol_lock_then_sleep, &data);
	// Wait until the worker is confirmed to hold the protocol mutex.
	while (!data.holding.is_set()) {
		OS::get_singleton()->delay_usec(1000);
	}

	// The invalidation funnel enqueues into the pending record and must complete without taking the
	// protocol mutex. If it blocked on the mutex it would still be waiting here (the worker clears
	// `holding` right before releasing the lock, 500 ms later), so a still-set flag proves it did
	// not block.
	FSLanguage::get_singleton()->notify_disk_source_changed(path);
	CHECK(data.holding.is_set());

	worker.wait_to_finish();

	FSCache::clear();
	memdelete(protocol);
	memdelete(editor_file_system);
}

TEST_CASE("[Modules][FoundryScript][LSP] Concurrent request handling and invalidation keep parse results consistent") {
	EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
	FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
	REQUIRE(protocol);
	TestFSLanguageProtocolInitializer::mark_initialized(protocol);

	const String path_a = "res://lsp/concurrency_stress_a.fs";
	const String path_b = "res://lsp/concurrency_stress_b.fs";
	const String source_a = "extends RefCounted\nfunc a() -> void:\n\tpass\n";
	const String source_b = "extends RefCounted\nfunc b() -> void:\n\tpass\n";

	const String uri_a = protocol->get_workspace()->get_file_uri(path_a);
	const String uri_b = protocol->get_workspace()->get_file_uri(path_b);
	{
		ScopedLSPTempFile file_a(path_a, source_a);
		ScopedLSPTempFile file_b(path_b, source_b);
		protocol->get_text_document()->didOpen(make_did_open_params(uri_a, source_a));
		protocol->get_text_document()->didOpen(make_did_open_params(uri_b, source_b));
	}

	REQUIRE(protocol->peek_parse_result(path_a) != nullptr);
	REQUIRE(protocol->peek_parse_result(path_b) != nullptr);

	StressWorkerData worker_data;
	worker_data.protocol = protocol;
	worker_data.path_a = path_a;

	Thread worker;
	worker.start(stress_worker, &worker_data);

	// Main thread concurrently invalidates path_a and edits path_b, both of which take the protocol
	// mutex; the worker's lock-scoped reads therefore always observe a consistent parse_results map.
	for (int i = 0; i < 200; i++) {
		FSLanguage::get_singleton()->notify_disk_source_changed(path_a);
		protocol->get_text_document()->didChange(make_did_change_params(uri_b, source_b));
	}

	worker.wait_to_finish();
	CHECK(worker_data.done.is_set());

	// Drain any deferred work and assert a consistent end state.
	protocol->apply_pending_invalidations();
	CHECK_EQ(worker_data.null_parser_seen.get(), 0);
	{
		MutexLock lock(protocol->mutex);
		CHECK(protocol->peek_parse_result(path_a) != nullptr);
		CHECK(protocol->peek_parse_result(path_b) != nullptr);
	}

	// At least one diagnostics notification was produced by the concurrent re-parses.
	CHECK_FALSE(TestFSLanguageProtocolInitializer::take_client_notifications(protocol, "textDocument/publishDiagnostics").is_empty());

	FSCache::clear();
	memdelete(protocol);
	memdelete(editor_file_system);
}

TEST_CASE("[Modules][FoundryScript][LSP] Conformance-namespace invalidation reaches open documents through the poll drain") {
	EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
	FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
	REQUIRE(protocol);
	TestFSLanguageProtocolInitializer::mark_initialized(protocol);

	const String path = "res://lsp/concurrency_namespace_doc.fs";
	const String source = "namespace lsp_concurrency_ns\nclass Widget:\n\tpass\n";
	ScopedLSPTempFile file(path, source);
	const String uri = protocol->get_workspace()->get_file_uri(path);
	protocol->get_text_document()->didOpen(make_did_open_params(uri, source));

	const ExtendFSParser *before = protocol->peek_parse_result(path);
	REQUIRE(before != nullptr);

	// The funnel must enqueue without re-parsing synchronously (D3): the cached parser is untouched.
	FSLanguage::get_singleton()->notify_conformance_namespace_changed("lsp_concurrency_ns");
	CHECK_EQ(protocol->peek_parse_result(path), before);

	// Clear any diagnostics queued by the initial open, then drain: the namespace resolves to this
	// open document and it is re-parsed under the protocol mutex.
	TestFSLanguageProtocolInitializer::take_client_notifications(protocol, "textDocument/publishDiagnostics");
	protocol->apply_pending_invalidations();

	// A fresh diagnostics notification proves the re-parse ran during the drain (the parser pointer
	// is not a reliable signal: memdelete + memnew can reuse the same address).
	REQUIRE(protocol->peek_parse_result(path) != nullptr);
	CHECK_FALSE(TestFSLanguageProtocolInitializer::take_client_notifications(protocol, "textDocument/publishDiagnostics").is_empty());

	FSCache::clear();
	memdelete(protocol);
	memdelete(editor_file_system);
}

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
