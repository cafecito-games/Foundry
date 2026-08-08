/**************************************************************************/
/*  http_server.h                                                         */
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

#include "http_server_connection.h"

#include "core/crypto/crypto.h"
#include "core/io/tcp_server.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/callable.h"
#include "scene/main/node.h"

// Registered under `foundry.http.server`, so it is reachable only through its qualified name.
//
// The server owns a listening `TCPServer` and one `HTTPServerConnection` per accepted socket.
//
// In the default frame-poll mode all work happens on the thread that calls `poll()`: while the node
// is in the tree that is the main thread through internal processing, and a headless caller can
// drive `poll()` itself.
//
// With `use_threads` a `WorkerThreadPool` task owns accepting connections and reading and parsing
// each one up to a complete request. It never runs script: a fully parsed request is handed to a
// mutex-guarded queue that the main thread drains from `poll()`, where the route `Callable` and the
// `request_received` signal are invoked. Observable behavior is identical to frame-poll mode.
class HTTPServer : public Node {
	FOUNDRY_CLASS(HTTPServer, Node);

	struct Route {
		String method;
		String path;
		Callable callable;
	};

	struct FileMount {
		// The matched prefix without its trailing separator, so a mount at `/` carries an empty
		// prefix and claims every path.
		String prefix;
		// An absolute directory with every symbolic link already followed, which is what a
		// requested path is checked against.
		String root;
	};

	int port = 8080;
	String bind_address = "127.0.0.1";
	bool emit_for_all = false;
	int max_connections = 64;
	// When set, `listen()` starts a worker task that accepts and parses connections off the main
	// thread; dispatch always stays on the main thread. Cannot change while the server is listening.
	bool use_threads = false;
	// Copied into every connection as it is accepted, so a limit changed while the server runs
	// applies to connections taken after the change and never rewrites one already in flight.
	HTTPServerConnection::Limits limits;
	// The server-side TLS options this listener terminates HTTPS with, or null for a plaintext
	// listener. Set from the `listen()` argument, so a plaintext `listen()` clears it and a socket is
	// never left half in TLS across restarts.
	Ref<TLSOptions> tls_options;

	Ref<TCPServer> tcp_server;
	LocalVector<Ref<HTTPServerConnection>> connections;
	LocalVector<Route> routes;
	LocalVector<FileMount> mounts;

	// Guards `connections`, `pending`, the `tcp_server` accept path, and each connection's
	// `awaiting_dispatch` flag, and orders every handoff of a connection between the worker and the
	// main thread. In frame-poll mode no worker runs and the mutex is uncontended.
	mutable Mutex mutex;
	WorkerThreadPool::TaskID worker_task = WorkerThreadPool::INVALID_TASK_ID;
	SafeFlag worker_should_exit;
	// Connections parked in `STATE_READY` by the worker, awaiting main-thread dispatch. Holding a
	// `Ref` keeps each alive independently of the `connections` list the worker prunes.
	LocalVector<Ref<HTTPServerConnection>> pending;

	void _dispatch(const Ref<HTTPServerConnection> &p_connection);
	// The frame-poll path: accept, drive every connection, dispatch the ready ones, prune the closed.
	void _poll_connections();
	// The threaded worker loop and its phases. The worker owns a connection until it reaches
	// `STATE_READY`, at which point ownership passes to the main thread through `pending` until
	// dispatch hands it back.
	static void _worker_thread_func(void *p_server);
	void _run_worker();
	void _accept_connections();
	void _service_connections();
	// The main-thread half of threaded mode: dispatch every parked request.
	void _drain_pending();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Port `0` asks the operating system for a free port, which `get_listening_port()` reports back.
	void set_port(int p_port);
	int get_port() const;

	// Defaults to the loopback interface, so the server is not reachable off the machine unless a
	// wider address is set on purpose.
	void set_bind_address(const String &p_bind_address);
	String get_bind_address() const;

	// When false the `request_received` signal only fires for requests no route claimed, which keeps
	// routed traffic off the signal. When true every request is announced, routed or not.
	void set_emit_for_all(bool p_emit_for_all);
	bool is_emitting_for_all() const;

	// How many sockets the server owns at once. A connection arriving while the cap is reached is
	// left waiting in the listen queue and is taken once a slot frees, so it costs nothing here.
	void set_max_connections(int p_max_connections);
	int get_max_connections() const;

	// A request whose body is larger than this is refused with `413` before any of it is buffered.
	void set_max_request_body_bytes(int p_max_request_body_bytes);
	int get_max_request_body_bytes() const;

	// A request carrying more header fields than this is refused with `431`.
	void set_max_header_count(int p_max_header_count);
	int get_max_header_count() const;

	// A request carrying a header line longer than this is refused with `431`.
	void set_max_header_line_bytes(int p_max_header_line_bytes);
	int get_max_header_line_bytes() const;

	// A request whose header block does not end within this many bytes is refused with `431`.
	void set_max_header_block_bytes(int p_max_header_block_bytes);
	int get_max_header_block_bytes() const;

	// When true, `listen()` runs accepting, reading and parsing on a `WorkerThreadPool` task while
	// dispatch stays on the main thread. Refused while the server is listening, since the mode is
	// wired up at `listen()` time.
	void set_use_threads(bool p_use_threads);
	bool is_using_threads() const;

	// Seconds a connection may go without moving a byte before it is dropped. Zero disables it.
	void set_connection_timeout_seconds(double p_connection_timeout_seconds);
	double get_connection_timeout_seconds() const;

	// Seconds one request has to finish, from when it begins until it is handed off for a response,
	// regardless of how recently a byte moved. Bounds a trickling peer that would otherwise reset the
	// progress timeout forever, and in threaded mode also reclaims a request stranded in the dispatch
	// queue by a stalled main-thread drain. Zero disables it.
	void set_request_deadline_seconds(double p_request_deadline_seconds);
	double get_request_deadline_seconds() const;

	// Binds `bind_address` on `port`. With no argument the listener is plaintext HTTP; passing
	// server-side `TLSOptions` terminates HTTPS on every accepted connection through the engine's
	// mbedTLS-backed `StreamPeerTLS`.
	Error listen(const Ref<TLSOptions> &p_tls_options = Ref<TLSOptions>());
	// Closes the listener and every open connection. Safe to call when not listening.
	void stop();
	bool is_listening() const;
	// The bound port while listening, or `-1` otherwise.
	int get_listening_port() const;

	// Registers an exact `method` + `path` handler, invoked with `(request, response)`. The first
	// registration for a given pair wins.
	void route(const String &p_method, const String &p_path, const Callable &p_callable);

	// Serves the files under `root_dir` for every request path starting with `url_prefix`. A mount
	// answers before any route, so a route can never shadow a file; a path the mount holds no file
	// for is left to the rest of the resolution ladder. A path that resolves outside `root_dir` —
	// through `..`, an escaping symbolic link, or anything else — is refused and never served.
	void mount_files(const String &p_url_prefix, const String &p_root_dir);

	// The number of registered mounts. Native inspection only.
	int get_mount_count() const;

	// Advances the server for one pass. In frame-poll mode this accepts, parses, dispatches and
	// writes; in threaded mode the worker owns accept/parse/write and this only dispatches the
	// requests it has parked. Called automatically while the node is in the tree.
	void poll();

	// The number of connections the server currently owns. Native inspection only.
	int get_connection_count() const;

	HTTPServer();
	~HTTPServer();
};
